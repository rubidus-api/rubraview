/*
 * Work on files: delete, rename and curate, the file on screen to a folder, what Explorer's menu asks,
 * classifying, and printing (§3.18, D-86, RV-133; RV-163, D-197: moved from main.c as it was).
 */
#include "app.h"
#include "rubraview/archive_job.h"

/* ---- triage: delete, rename, curate (§3.18) ---- */

/* The file behind the page on screen, or an empty slice for an archive
   page — a page inside a CBZ is not a file, so none of §3.18 applies to
   it and every action below refuses rather than doing something to the
   archive. */
proven_u8str_view_t current_file_path(app_state_t *app) {
    int32_t page = current_page_index(app);
    if (page < 0 || app->source.kind != RUBRAVIEW_PAGE_SOURCE_FOLDER) {
        return PROVEN_LIT("");
    }
    return app->source.pages[page].path;
}

/* After a file leaves the sequence the viewer has to land somewhere.
   §3.18.1: the next item, or the one before when the last was removed. */
void reopen_after_removal(app_state_t *app, size_t removed_index) {
    proven_u8str_view_t dir = app->source_dir;
    if (dir.size == 0) return;

    size_t target = removed_index;
    open_folder(app, dir);
    if (page_count(app) == 0) return;
    if (target >= page_count(app)) target = page_count(app) - 1;

    finish_open(app, target);
}

/* D-128: the folder again, on a file by its path — a file was added beside it,
   so its place in the order (by date, say) may not be what it was. */
void reopen_on_path(app_state_t *app, proven_u8str_view_t path, size_t fallback) {
    proven_u8str_view_t dir = app->source_dir;
    if (dir.size == 0) return;
    char kept[1024];
    if (path.size >= sizeof(kept)) { reopen_after_removal(app, fallback); return; }
    memcpy(kept, path.ptr, path.size);                 /* the list it may point into is about to be made again */
    proven_u8str_view_t want = { .ptr = (const proven_byte_t *)kept, .size = path.size };
    open_folder(app, dir);
    if (page_count(app) == 0) return;
    size_t target = fallback < page_count(app) ? fallback : page_count(app) - 1;
    for (size_t i = 0; i < page_count(app); ++i) {
        if (proven_u8str_view_eq(app->source.pages[i].path, want)) { target = i; break; }
    }
    finish_open(app, target);
}

void osd_say(app_state_t *app, proven_u8str_view_t text) {
    size_t n = text.size < sizeof(app->notice) - 1 ? text.size : sizeof(app->notice) - 1;
    memcpy(app->notice, text.ptr, n);
    app->notice[n] = '\0';
    app->notice_length = n;
    app->notice_seconds = 2.5;
}

void triage_delete(app_state_t *app, bool permanent) {
    proven_u8str_view_t path = current_file_path(app);
    if (path.size == 0) return;

    int32_t page = current_page_index(app);
    if (page < 0) return;

    bool ok = permanent ? rubraview_pal_fs_delete(path) : rubraview_pal_fs_recycle(path);
    if (!ok) {
        osd_say(app, TU8("could not delete that file"));
        return;
    }

    rubraview_undo_push(&app->undo, (rubraview_file_action_t){
        .op = permanent ? RUBRAVIEW_FILE_OP_PURGE : RUBRAVIEW_FILE_OP_RECYCLE,
        .source_path = path,
        .playlist_index = (size_t)page,
    });

    osd_say(app, permanent ? TU8("deleted permanently") : TU8("moved to the recycle bin"));
    reopen_after_removal(app, (size_t)page);
}

void triage_curate(app_state_t *app, int32_t digit) {
    proven_u8str_view_t path = current_file_path(app);
    if (path.size == 0) return;

    proven_u8str_view_t target_dir = rubraview_curation_target(&app->curation, digit);
    if (target_dir.size == 0) return;   /* that key is not bound: do nothing */

    int32_t page = current_page_index(app);
    if (page < 0) return;

    rubraview_pal_fs_make_dirs(target_dir);
    proven_u8str_view_t name = rubraview_path_basename(path);
    proven_u8str_view_t target = rubraview_path_join(app->arena, target_dir, name);
    if (target.size == 0) return;

    bool moving = app->curation.mode == RUBRAVIEW_CURATION_MOVE;
    bool ok = moving ? rubraview_pal_fs_move(path, target) : rubraview_pal_fs_copy(path, target);
    if (!ok) {
        osd_say(app, TU8("could not put the file there"));
        return;
    }

    rubraview_undo_push(&app->undo, (rubraview_file_action_t){
        .op = moving ? RUBRAVIEW_FILE_OP_MOVE : RUBRAVIEW_FILE_OP_COPY,
        .source_path = path,
        .target_path = target,
        .playlist_index = (size_t)page,
    });

    osd_say(app, moving ? TU8("moved") : TU8("copied"));

    /* §3.18.3: a move takes the file out of the sequence, so the viewer
       advances; a copy leaves it, so it stays put. */
    if (rubraview_curation_advances(&app->curation)) {
        reopen_after_removal(app, (size_t)page);
    }
}

/* ---- the file on screen, to its folder (owner, 2026-10-08) ----
   `copy_to_folder` and `move_to_folder`, each with a folder of its own from
   Settings > Files. A name already there is never replaced: the file goes
   in as `name-1.ext`, `name-2.ext`, ... A page inside an archive is not a
   file, so it can be copied out (its bytes written as a file) but not
   moved. */
bool filing_exists(void *ctx, proven_u8str_view_t path) {
    (void)ctx;
    return rubraview_pal_fs_exists(path);
}

void filing_put(app_state_t *app, bool moving) {
    if (current_page_index(app) < 0) return;
    proven_u8str_view_t dir = moving ? app->curation.move_dir : app->curation.copy_dir;
    /* None set yet: the box asks for one, as Shift+F6 / Shift+F7 do. */
    if (dir.size == 0) { target_begin(app, moving, false); return; }
    filing_put_to(app, dir, moving);
}

void filing_put_to(app_state_t *app, proven_u8str_view_t dir, bool moving) {
    int32_t page = current_page_index(app);
    if (page < 0) return;
    bool in_archive = app->source.kind != RUBRAVIEW_PAGE_SOURCE_FOLDER;
    if (in_archive && moving) {
        osd_say(app, TU8("a page inside an archive can be copied (Ctrl+Shift+C), not moved"));
        return;
    }
    proven_u8str_view_t path = in_archive ? PROVEN_LIT("") : app->source.pages[page].path;
    proven_u8str_view_t name = rubraview_path_basename(in_archive ? app->source.pages[page].name : path);
    if (name.size == 0) return;

    if (!rubraview_pal_fs_make_dirs(dir) && !rubraview_pal_fs_exists(dir)) {
        osd_say(app, TU8("that folder could not be made"));
        return;
    }
    proven_u8str_view_t target = rubraview_filing_target(app->arena, dir, name, filing_exists, NULL);
    if (target.size == 0) {
        osd_say(app, TU8("no free name left in that folder"));
        return;
    }
    /* The same file onto itself would be a copy named -1 beside it: allowed;
       a move into the folder it is already in is nothing to do. */
    if (moving && rubraview_path_same(rubraview_path_dirname(path), dir)) {
        osd_say(app, TU8("it is already in that folder"));
        return;
    }

    bool ok = false;
    if (in_archive) {
        void *mem = NULL;
        rubraview_page_bytes_t bytes = page_read_owned(&app->source, &app->source_lock, (size_t)page, &mem);
        ok = bytes.ok && rubraview_pal_fs_write_file(target, bytes.data);
        free(mem);
    } else {
        ok = moving ? rubraview_pal_fs_move(path, target) : rubraview_pal_fs_copy(path, target);
    }
    if (!ok) {
        osd_say(app, TU8("could not put the file there"));
        return;
    }

    rubraview_undo_push(&app->undo, (rubraview_file_action_t){
        .op = moving ? RUBRAVIEW_FILE_OP_MOVE : RUBRAVIEW_FILE_OP_COPY,
        .source_path = path,
        .target_path = target,
        .playlist_index = (size_t)page,
    });

    /* Say the name it has there: it is not always the name it had. */
    char said[sizeof(app->notice)];
    proven_u8str_view_t put = rubraview_path_basename(target);
    int n = snprintf(said, sizeof(said), "%s: %.*s", moving ? T("moved") : T("copied"), (int)(put.size < 160 ? put.size : 160), put.ptr);
    osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)said, .size = n > 0 ? (size_t)n : 0 });

    if (moving) reopen_after_removal(app, (size_t)page);
}

void triage_undo(app_state_t *app) {
    rubraview_file_action_t action = {0};
    switch (rubraview_undo_peek(&app->undo, &action)) {
        case RUBRAVIEW_UNDO_NOTHING:
            osd_say(app, TU8("nothing to undo"));
            return;
        case RUBRAVIEW_UNDO_IRREVERSIBLE:
            /* Said plainly rather than silently undoing the action
               before it, which would restore the wrong file. */
            osd_say(app, TU8("that file was deleted permanently and cannot be brought back"));
            return;
        case RUBRAVIEW_UNDO_AVAILABLE:
            break;
    }

    /* 1.23.0: the files of one move or copy of many go back together, newest first. */
    size_t undone = 0;
    for (;;) {
        bool ok = false;
        switch (action.op) {
            case RUBRAVIEW_FILE_OP_MOVE:
            case RUBRAVIEW_FILE_OP_RENAME:
                ok = rubraview_pal_fs_move(action.target_path, action.source_path);
                break;
            case RUBRAVIEW_FILE_OP_COPY:
                /* Undoing a copy means removing the copy, not the original; a folder's copy whole (1.23.0). */
                ok = rubraview_pal_fs_delete(action.target_path) || rubraview_pal_fs_delete_tree(action.target_path);
                break;
            case RUBRAVIEW_FILE_OP_RECYCLE:
                ok = rubraview_pal_fs_restore_last_recycled(action.source_path);
                if (!ok) osd_say(app, TU8("Windows keeps the recycle bin's undo to itself — restore it from there"));
                break;
            default:
                break;
        }
        if (!ok) {
            if (undone > 0) break;
            if (action.op == RUBRAVIEW_FILE_OP_COPY) osd_say(app, TU8("could not take the copy back"));
            return;
        }
        rubraview_undo_commit(&app->undo);
        undone++;
        rubraview_file_action_t next = {0};
        if (!rubraview_undo_same_batch(&app->undo, action.batch) ||
            rubraview_undo_peek(&app->undo, &next) != RUBRAVIEW_UNDO_AVAILABLE) break;
        action = next;
    }

    if (undone > 1) {
        char line[96];
        int n = snprintf(line, sizeof(line), T("%zu undone"), undone);
        osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = n > 0 ? (size_t)n : 0 });
    } else {
        osd_say(app, TU8("undone"));
    }
    if (app->picker_open) picker_navigate(app, app->picker_dir);   /* what it lists has changed */
    else reopen_after_removal(app, action.playlist_index);
}

/* §3.18.2's inline rename. The box is drawn here, not a child EDIT
   control; its text comes from the window's text input (TEXT events,
   with the IME attached only while the box is open, K5), so Hangul,
   capitals and symbols go in as typed. The caret moves and selects with
   the arrows, Home and End (Shift to select), Ctrl+A selects all, and
   Ctrl+C / Ctrl+X / Ctrl+V use the clipboard. */
/* The box closes: the window goes back to keys only (K5). */
void rename_end(app_state_t *app) {
    app->rename_active = false;
    app->rename_btn_n = 0;
    app->rename_is_archive = false;
    app->rename_is_extension = false;
    app->rename_is_picker = false;
    app->rename_is_path = false;
    app->rename_is_filter = false;
    app->rename_is_listfilter = false;
    app->rename_is_target = false;
    app->rename_is_pack = false;
    app->rename_is_find = false;
    app->rename_is_value = false;
    app->rename_is_gmic = false;
    app->rename_is_ffmpeg = false;
    app->rename_is_address = false;
    app->batch_typing = 0;
    app->rename_is_apply = false;
    app->target_offered_len = 0;
    if (app->rename_is_password) {                       /* nothing of it stays behind */
        secure_wipe(app->rename_buffer, sizeof(app->rename_buffer));
        secure_wipe(app->rename_composing, sizeof(app->rename_composing));
        app->rename_edit = rubraview_textedit_make(app->rename_buffer, sizeof(app->rename_buffer));
        app->rename_is_password = false;
    }
    app->rename_favorite = -1;
    app->rename_composing_length = 0;
    rubraview_pal_window_text_input(app->window, false);
}

/* Text typed or finished by the IME, and the IME's unfinished part. */
void rename_text(app_state_t *app, const rubraview_window_event_t *event) {
    if (app->ab_edit_open) {
        if (event->kind == RUBRAVIEW_WINDOW_EVENT_TEXT) {
            ab_edit_text_typed(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)event->text.utf8, .size = event->text.length });
        }
        return;
    }
    if (app->px_edit_open) {
        if (event->kind == RUBRAVIEW_WINDOW_EVENT_TEXT) {
            px_edit_text_typed(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)event->text.utf8, .size = event->text.length });
        }
        return;
    }
    if (!app->rename_active) return;
    proven_u8str_view_t text = { .ptr = (const proven_byte_t *)event->text.utf8, .size = event->text.length };
    if (event->kind == RUBRAVIEW_WINDOW_EVENT_TEXT) {
        rename_insert(app, text);
    } else {
        size_t n = text.size < sizeof(app->rename_composing) ? text.size : sizeof(app->rename_composing) - 1;
        memcpy(app->rename_composing, text.ptr, n);
        app->rename_composing_length = n;
    }
}

/* The picker's path box (owner, 2026-09-28: "경로를 직접 입력하는 기능도
   당연히 필요해요"): the rename box over the path bar, holding the folder
   on screen, all of it selected so typing replaces it. */
void picker_path_begin(app_state_t *app) {
    if (!app->picker_open || app->rename_active) return;
    proven_u8str_view_t here = rubraview_picker_is_places(app->picker_dir) ? PROVEN_LIT("") : app->picker_dir;
    app->rename_edit = rubraview_textedit_make(app->rename_buffer, sizeof(app->rename_buffer));
    rubraview_textedit_set(&app->rename_edit, here.size < sizeof(app->rename_buffer) ? here : PROVEN_LIT(""));
    rubraview_textedit_select_all(&app->rename_edit);
    app->rename_active = true;
    app->rename_is_path = true;
    app->rename_composing_length = 0;
    rubraview_pal_window_text_input(app->window, true);
}

/* Enter in the path box: a folder is shown, a file is opened, and a path
   that is not there is said, with the box left open to put it right. */
static void picker_path_commit(app_state_t *app) {
    rename_insert(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)app->rename_composing, .size = app->rename_composing_length });
    app->rename_composing_length = 0;
    proven_u8str_view_t path = rubraview_picker_typed_path(app->arena, rubraview_textedit_text(&app->rename_edit), app->picker_dir);
    if (path.size == 0) { rename_end(app); return; }
    rubraview_fs_entry_t found;
    if (!rubraview_pal_fs_stat(app->arena, path, &found)) {
        osd_say(app, TU8("no such folder or file"));
        return;
    }
    rename_end(app);
    if (found.is_directory) {
        picker_navigate(app, path);
        app->picker.focus = 0;
    } else {
        app->picker_open = false;
        open_path(app, path);
    }
}

/* The folder box for a copy or a move (owner, 2026-10-16): the last
   folder typed in it, or the one set for F6 / F7, all selected. */
void target_begin(app_state_t *app, bool moving, bool from_picker) {
    if (from_picker) {
        size_t picked = 0;
        uint64_t bytes = 0;
        rubraview_picker_selection_metrics(&app->picker, &picked, &bytes);
        if (picked == 0) { osd_say(app, TU8("pick some files first")); return; }
    } else {
        if (current_page_index(app) < 0) return;
        if (moving && app->source.kind != RUBRAVIEW_PAGE_SOURCE_FOLDER) {
            osd_say(app, TU8("a page inside an archive can be copied (Ctrl+Shift+C), not moved"));
            return;
        }
    }
    if (app->rename_active) rename_end(app);
    proven_u8str_view_t last = rubraview_settings_get_text(&app->settings, PROVEN_LIT("curation"), PROVEN_LIT("other_dir"));
    if (last.size == 0) last = moving ? app->curation.move_dir : app->curation.copy_dir;
    app->rename_edit = rubraview_textedit_make(app->rename_buffer, sizeof(app->rename_buffer));
    rubraview_textedit_set(&app->rename_edit, last.size < sizeof(app->rename_buffer) ? last : PROVEN_LIT(""));
    rubraview_textedit_select_all(&app->rename_edit);
    app->rename_active = true;
    app->rename_is_target = true;
    app->target_moving = moving;
    app->target_from_picker = from_picker;
    app->target_offered_len = 0;
    app->rename_composing_length = 0;
    rubraview_pal_window_text_input(app->window, true);
    osd_say(app, moving ? TU8("type the folder to move to; Ctrl+O browses, a new one is made")
                        : TU8("type the folder to copy to; Ctrl+O browses, a new one is made"));
}

void target_browse(app_state_t *app) {
    rubraview_file_dialog_opts_t opts = {
        .title = app->target_moving ? T("Move to which folder") : T("Copy to which folder"), .folder_mode = true,
        .parent_window_handle = rubraview_pal_window_native_handle(app->window),
    };
    rubraview_file_dialog_result_t picked = rubraview_pal_file_dialog_pick_folder(app->arena, &opts);
    if (picked.accepted && picked.count > 0 && picked.paths[0].size < sizeof(app->rename_buffer)) {
        rubraview_textedit_set(&app->rename_edit, picked.paths[0]);   /* in the box, for Enter */
        app->target_offered_len = 0;
    }
}

/* Enter in the folder box: a folder there takes the files; one not there
   is offered, and made when Enter comes again for it. */
void target_commit(app_state_t *app) {
    rename_insert(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)app->rename_composing, .size = app->rename_composing_length });
    app->rename_composing_length = 0;
    proven_u8str_view_t typed = rubraview_target_folder_trim(rubraview_textedit_text(&app->rename_edit));
    proven_u8str_view_t base = app->target_from_picker ? app->picker_dir : rubraview_path_dirname(current_file_path(app));
    proven_u8str_view_t path = typed.size ? rubraview_picker_typed_path(app->arena, typed, base) : typed;
    rubraview_fs_entry_t found;
    bool exists = path.size > 0 && rubraview_pal_fs_stat(app->arena, path, &found);
    proven_u8str_view_t offered = { .ptr = (const proven_byte_t *)app->target_offered, .size = app->target_offered_len };
    char line[1200];
    switch (rubraview_target_folder_step(path, exists, exists && found.is_directory, offered)) {
        case RUBRAVIEW_TARGET_NOTHING:
            rename_end(app);
            return;
        case RUBRAVIEW_TARGET_NOT_FOLDER:
            osd_say(app, TU8("that is a file, not a folder - put it right, or Esc"));
            return;
        case RUBRAVIEW_TARGET_OFFER: {
            size_t n = path.size < sizeof(app->target_offered) ? path.size : 0;
            memcpy(app->target_offered, path.ptr, n);
            app->target_offered_len = n;
            int w = snprintf(line, sizeof(line), T("%.*s is not there yet: Enter again makes it, Esc leaves it"),
                             (int)path.size, path.ptr);
            if (w > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)w < sizeof(line) ? (size_t)w : sizeof(line) - 1 });
            return;
        }
        case RUBRAVIEW_TARGET_MAKE:
            if (!rubraview_pal_fs_make_dirs(path) && !rubraview_pal_fs_exists(path)) {
                osd_say(app, TU8("that folder could not be made"));
                return;
            }
            break;
        case RUBRAVIEW_TARGET_USE:
            break;
    }
    /* Kept, to be offered first next time. */
    proven_u8str_view_t kept = app_keep(app, path);
    rubraview_settings_set_text(&app->settings, PROVEN_LIT("curation"), PROVEN_LIT("other_dir"), kept);
    settings_write_if_changed(app, false);
    bool moving = app->target_moving, from_picker = app->target_from_picker;
    rename_end(app);
    if (from_picker) picker_curate_to(app, kept, moving);
    else filing_put_to(app, kept, moving);
}

void rename_begin(app_state_t *app) {
    proven_u8str_view_t path = current_file_path(app);
    /* D-88: a page of an archive is renamed where it is, in its folder there. */
    bool in_archive = on_archive_page(app);
    if (in_archive) {
        if (app->arc_job) return;
        rubraview_arcedit_route_t route = rubraview_arcedit_route(&app->source);
        if (route == RUBRAVIEW_ARCEDIT_ENCRYPTED) { osd_say(app, cstr(rubraview_arcedit_result_text(RUBRAVIEW_ARCEDIT_REFUSED_ENCRYPTED))); return; }
        path = page_display_name(app, (size_t)current_page_index(app));
    }
    if (path.size == 0) return;

    /* The whole name, extension and all (owner, 2026-09-23): an
       extension typed wrongly could not be corrected before, and the
       owner asked for no question when it changes. */
    proven_u8str_view_t name = rubraview_path_basename(path);
    if (name.size >= sizeof(app->rename_buffer)) return;

    app->rename_edit = rubraview_textedit_make(app->rename_buffer, sizeof(app->rename_buffer));
    rubraview_textedit_set(&app->rename_edit, name);
    app->rename_active = true;
    app->rename_is_archive = in_archive;
    app->rename_composing_length = 0;
    rubraview_pal_window_text_input(app->window, true);   /* Hangul, capitals, symbols (K5's hole) */
}

/* 1.24.0 (owner, 2026-10-19: "선택된 파일들의 확장자들에 대해 각각 어떤 것으로 바꿀지를 선택"): Change extensions
   lists each extension the picked files have, its new one beside it; the box edits one row at a time
   (a click on a row, Tab or Enter goes to another), and Apply (Enter on the last) renames them all.
   A row left as it was leaves its files alone. */
static bool ext_same(const char *a, proven_u8str_view_t b) {
    return proven_u8str_view_eq_ascii_ci(proven_u8str_view_from_cstr(a), b);
}

static void ext_store(app_state_t *app) {
    if (app->ext_at < 0 || app->ext_at >= app->ext_n) return;
    proven_u8str_view_t typed = rubraview_textedit_text(&app->rename_edit);
    snprintf(app->ext_to[app->ext_at], sizeof(app->ext_to[0]), "%.*s", (int)typed.size, typed.ptr);
}

void ext_switch(app_state_t *app, int32_t row) {
    if (row < 0 || row >= app->ext_n) return;
    ext_store(app);
    app->ext_at = row;
    rubraview_textedit_set(&app->rename_edit, cstr(app->ext_to[row]));
    rubraview_textedit_select_all(&app->rename_edit);
    app->rename_composing_length = 0;
}

void rename_extension_begin(app_state_t *app) {
    app->ext_n = 0;
    for (size_t i = 0; i < app->picker_listing.count && app->ext_n < 16; ++i) {
        if (!app->picker_selected || !app->picker_selected[i] || app->picker_listing.entries[i].is_directory) continue;
        proven_u8str_view_t ext = rubraview_path_ext(app->picker_listing.entries[i].name);
        if (ext.size == 0 || ext.size >= sizeof(app->ext_from[0])) continue;
        bool seen = false;
        for (int32_t k = 0; k < app->ext_n && !seen; ++k) seen = ext_same(app->ext_from[k], ext);
        if (seen) continue;
        snprintf(app->ext_from[app->ext_n], sizeof(app->ext_from[0]), "%.*s", (int)ext.size, ext.ptr);
        snprintf(app->ext_to[app->ext_n], sizeof(app->ext_to[0]), "%.*s", (int)ext.size, ext.ptr);
        app->ext_n++;
    }
    if (app->ext_n == 0) { osd_say(app, TU8("pick some files with an extension first")); return; }
    app->ext_at = 0;
    app->rename_edit = rubraview_textedit_make(app->rename_buffer, sizeof(app->rename_buffer));
    rubraview_textedit_set(&app->rename_edit, cstr(app->ext_to[0]));
    rubraview_textedit_select_all(&app->rename_edit);
    app->rename_active = true;
    app->rename_is_extension = true;
    app->rename_composing_length = 0;
    rubraview_pal_window_text_input(app->window, true);
}

/* Every picked file whose extension has a new one, renamed to it; one undo for all. */
void rename_extension_apply(app_state_t *app) {
    ext_store(app);
    size_t done = 0, failed = 0;
    uint32_t batch = ++app->undo_batch;
    for (size_t i = 0; i < app->picker_listing.count; ++i) {
        if (!app->picker_selected || !app->picker_selected[i]) continue;
        const rubraview_fs_entry_t *entry = &app->picker_listing.entries[i];
        if (entry->is_directory) continue;
        proven_u8str_view_t ext = rubraview_path_ext(entry->name);
        int32_t row = -1;
        for (int32_t k = 0; k < app->ext_n && row < 0; ++k) if (ext.size > 0 && ext_same(app->ext_from[k], ext)) row = k;
        if (row < 0) continue;
        char want[66];
        const char *to = app->ext_to[row];
        while (*to == ' ') ++to;
        if (!*to || strcmp(to, app->ext_from[row]) == 0 || (to[0] != '.' && strcmp(to, app->ext_from[row] + 1) == 0)) continue;   /* left as it was */
        snprintf(want, sizeof(want), "%s%s", to[0] == '.' ? "" : ".", to);
        proven_u8str_view_t target = rubraview_path_with_ext(app->arena, entry->path, want);
        if (target.size == 0 || rubraview_rename_validate(rubraview_path_basename(target)) != RUBRAVIEW_RENAME_OK ||
            rubraview_pal_fs_exists(target) || !rubraview_pal_fs_move(entry->path, target)) { failed++; continue; }
        rubraview_undo_push(&app->undo, (rubraview_file_action_t){
            .op = RUBRAVIEW_FILE_OP_RENAME, .source_path = app_keep(app, entry->path), .target_path = target, .batch = batch,
        });
        done++;
    }
    rename_end(app);
    char line[128];
    int written = failed > 0 ? snprintf(line, sizeof(line), T("%zu renamed, %zu could not be"), done, failed)
                             : snprintf(line, sizeof(line), T("%zu file(s) renamed"), done);
    if (written > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)written });
    picker_navigate(app, app->picker_dir);   /* the names on screen have changed */
}

/* Enter: the next row, and Apply on the last. */
static void rename_extension_commit(app_state_t *app) {
    rename_insert(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)app->rename_composing, .size = app->rename_composing_length });
    app->rename_composing_length = 0;
    if (app->ext_at + 1 < app->ext_n) ext_switch(app, app->ext_at + 1);
    else rename_extension_apply(app);
}

/* 1.23.0 (owner, 2026-10-19): a file or a folder of the open-file screen renamed from its right click.
   The box is the rename box, holding its whole name. */
void rename_picker_begin(app_state_t *app, size_t at) {
    if (at >= app->picker_listing.count || app->rename_active) return;
    if (rubraview_picker_is_places(app->picker_dir) || rubraview_picker_is_recent(app->picker_dir) || app->picker_in_book ||
        picker_in_archive(app)) {
        osd_say(app, TU8("this list's names cannot be changed here"));
        return;
    }
    proven_u8str_view_t name = app->picker_listing.entries[at].name;
    if (name.size == 0 || name.size >= sizeof(app->rename_buffer)) return;
    app->rename_edit = rubraview_textedit_make(app->rename_buffer, sizeof(app->rename_buffer));
    rubraview_textedit_set(&app->rename_edit, name);
    app->rename_active = true;
    app->rename_is_picker = true;
    app->rename_picker_at = at;
    app->rename_composing_length = 0;
    rubraview_pal_window_text_input(app->window, true);
}

void rename_picker_commit(app_state_t *app) {
    rename_insert(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)app->rename_composing, .size = app->rename_composing_length });
    size_t at = app->rename_picker_at;
    rename_end(app);
    if (at >= app->picker_listing.count) return;
    const rubraview_fs_entry_t *entry = &app->picker_listing.entries[at];
    proven_u8str_view_t typed = rubraview_textedit_text(&app->rename_edit);
    if (typed.size == entry->name.size && memcmp(typed.ptr, entry->name.ptr, typed.size) == 0) return;   /* nothing changed */
    rubraview_rename_err_t err = rubraview_rename_validate(typed);
    if (err != RUBRAVIEW_RENAME_OK) { osd_say(app, rubraview_rename_error_text(err)); return; }
    proven_u8str_view_t source = app_keep(app, entry->path);
    proven_u8str_view_t target = rubraview_path_join(app->arena, rubraview_path_dirname(source), typed);
    if (target.size == 0 || rubraview_pal_fs_exists(target) || !rubraview_pal_fs_move(source, target)) {
        osd_say(app, TU8("could not rename that file"));
        return;
    }
    rubraview_undo_push(&app->undo, (rubraview_file_action_t){
        .op = RUBRAVIEW_FILE_OP_RENAME, .source_path = source, .target_path = target,
    });
    osd_say(app, TU8("renamed"));
    picker_navigate(app, app->picker_dir);   /* the name on screen has changed */
}

void rename_commit(app_state_t *app) {
    if (!app->rename_active) return;
    if (app->rename_is_find) { text_find_step(app, true); return; }   /* the box stays for the next */
    if (app->rename_is_value && app->batch_typing) { batch_typed(app); return; }   /* RV-200: Batch's own panel */
    if (app->rename_is_value) { edit_value_typed(app); return; }
    if (app->rename_is_gmic) { gmic_typed(app); return; }   /* RFC-0009 C */
    if (app->rename_is_ffmpeg) { ffmpeg_typed(app); return; }   /* RFC-0009 D */
    if (app->rename_is_address) { address_typed(app); return; }   /* RV-158 */
    if (app->batch_typing) { batch_typed(app); return; }
    if (app->rename_is_apply) { apply_all_typed(app); return; }
    if (app->rename_is_extension) { rename_extension_commit(app); return; }
    if (app->rename_is_picker) { rename_picker_commit(app); return; }
    if (app->rename_is_path) { picker_path_commit(app); return; }
    if (app->rename_is_filter) { picker_filter_follow(app); rename_end(app); return; }   /* 1.22.0: kept */
    if (app->rename_is_listfilter) { list_filter_follow(app); rename_end(app); return; }   /* 1.25.0: kept */
    if (app->rename_is_target) { target_commit(app); return; }
    if (app->rename_is_pack) { archwork_pack_typed(app); return; }   /* RV-207 */
    if (app->rename_is_password) { password_commit(app); return; }
    if (app->rename_favorite >= 0) { favorite_rename_commit(app); return; }
    /* Enter with a syllable still being built: it is part of the name. */
    rename_insert(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)app->rename_composing, .size = app->rename_composing_length });
    bool in_archive = app->rename_is_archive;
    rename_end(app);

    if (in_archive) {
        /* D-88: the same rules for the name; the archive is written again with it. */
        proven_u8str_view_t typed = rubraview_textedit_text(&app->rename_edit);
        rubraview_rename_err_t bad = rubraview_rename_validate(typed);
        char name[512];
        if (bad != RUBRAVIEW_RENAME_OK) { osd_say(app, rubraview_rename_error_text(bad)); return; }
        if (!on_archive_page(app) || typed.size >= sizeof(name)) return;
        proven_u8str_view_t old = rubraview_path_basename(page_display_name(app, (size_t)current_page_index(app)));
        if (old.size == typed.size && memcmp(old.ptr, typed.ptr, old.size) == 0) return;   /* nothing changed */
        memcpy(name, typed.ptr, typed.size);
        name[typed.size] = '\0';
        arc_edit_on_page(app, RUBRAVIEW_ARCEDIT_RENAME, name);
        return;
    }

    proven_u8str_view_t path = current_file_path(app);
    if (path.size == 0) return;

    proven_u8str_view_t new_name = rubraview_textedit_text(&app->rename_edit);

    rubraview_rename_err_t err = rubraview_rename_validate(new_name);
    if (err != RUBRAVIEW_RENAME_OK) {
        osd_say(app, rubraview_rename_error_text(err));
        return;
    }

    proven_u8str_view_t target = rubraview_path_join(app->arena, rubraview_path_dirname(path), new_name);
    if (target.size == 0 || !rubraview_pal_fs_move(path, target)) {
        osd_say(app, TU8("could not rename that file"));
        return;
    }

    int32_t page = current_page_index(app);
    rubraview_undo_push(&app->undo, (rubraview_file_action_t){
        .op = RUBRAVIEW_FILE_OP_RENAME,
        .source_path = path,
        .target_path = target,
        .playlist_index = page >= 0 ? (size_t)page : 0,
    });

    osd_say(app, TU8("renamed"));
    reopen_after_removal(app, page >= 0 ? (size_t)page : 0);
}

/* §3.19.2: what to do with what was dropped. */
/* §3.19.2: the files that were dropped, in natural name order, as a
   source of their own. They may come from different folders; the page
   list holds full paths, so that costs nothing. Directories among them
   are left out — rubraview_drop_classify sends a lone folder down the
   other path, and a folder dropped together with files has no obvious
   place in a sequence of pages. */
static void open_dropped_files(app_state_t *app, const rubraview_drop_item_t *items, size_t count) {
    if (count == 0) return;

    proven_result_mem_mut_t res = rubraview_arena_alloc_array(app->arena, count, sizeof(rubraview_fs_entry_t));
    if (!proven_is_ok(res.err)) return;
    rubraview_fs_entry_t *entries = (rubraview_fs_entry_t*)(void*)res.value.ptr;

    size_t n = 0;
    for (size_t i = 0; i < count; ++i) {
        if (items[i].is_directory) continue;
        rubraview_fs_entry_t entry;
        if (!rubraview_pal_fs_stat(app->arena, items[i].path, &entry)) continue;
        if (entry.is_directory) continue;
        entries[n++] = entry;
    }
    if (n == 0) return;

    char line[96];
    size_t pos = 0;
    line[0] = '\0';
    append_number(line, sizeof(line), &pos, n);
    append_text(line, sizeof(line), &pos, " dropped files opened as one set");
    open_set_from_entries(app, entries, n, cstr(line));
    recent_note_shown(app);
}

/* A given set of files becomes the page sequence — a drop, or what the
   picker picked. `said` goes to the OSD when it opens. */
/* Settings > Files > Sort by and Ascending: the order a folder's pages
   are turned in (owner, 2026-09-28). An archive keeps its natural order. */
rubraview_sort_mode_t folder_sort_mode(const app_state_t *app, bool *out_ascending) {
    *out_ascending = g_cli_descending >= 0 ? g_cli_descending == 0
                   : rubraview_settings_get(&app->settings, PROVEN_LIT("files"), PROVEN_LIT("sort_ascending")) > 0.5;
    if (g_cli_sort >= 0) return (rubraview_sort_mode_t)g_cli_sort;
    return rubraview_sort_mode_for_setting(
        (int32_t)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("files"), PROVEN_LIT("sort_mode"))));
}

/* `as_given` keeps the entries in the order they come in (a list a file
   was added to the end of); `start` is the file to open on, the first
   when it is empty or not among them. */
void open_set_ordered(app_state_t *app, rubraview_fs_entry_t *entries, size_t count, proven_u8str_view_t said,
                             bool as_given, proven_u8str_view_t start) {
    if (!entries || count == 0) return;

    history_remember(app);
    source_close(app);

    rubraview_fs_listing_t listing = { .entries = entries, .count = count };
    bool ascending = true;
    rubraview_sort_mode_t mode = folder_sort_mode(app, &ascending);
    if (as_given) mode = RUBRAVIEW_SORT_AS_GIVEN;
    app->list_is_set = true;   /* a playlist or picked files: the playlist window says so */
    char kinds_buf[1024];   /* 1.25.0: text files too while the text viewer is on (a sorted folder kept them out) */
    app->source = rubraview_page_source_from_listing(app->arena, &listing,
                                                     kinds_with_text(kinds_buf, sizeof(kinds_buf), IMAGE_FILTER ";" MEDIA_FILTER), mode, ascending);
    if (app->source.page_count == 0) {
        osd_say(app, TU8("none of those files is a picture or a film"));
        return;
    }
    /* The folder of the first one is where Rename, Delete and a batch
       run work; a set of files has no folder of its own. */
    app->source_dir = rubraview_path_dirname(entries[0].path);
    for (size_t i = 0; i < count; ++i) {   /* RV-157: a station has no folder; the first file's */
        if (!is_station_path(entries[i].path)) { app->source_dir = rubraview_path_dirname(entries[i].path); break; }
    }
    app->resume_offer = false;
    app->picker_open = false;
    int32_t first = start.size > 0 ? rubraview_page_source_find(&app->source, start) : -1;
    finish_open(app, first > 0 ? (size_t)first : 0);
    osd_say(app, said);
}

void open_set_from_entries(app_state_t *app, rubraview_fs_entry_t *entries, size_t count, proven_u8str_view_t said) {
    open_set_ordered(app, entries, count, said, false, PROVEN_LIT(""));
}

/* ---- D-86: what Explorer's menu asks ---- */

/* The files named, as one set of pages. */
void open_paths_as_set(app_state_t *app, const proven_u8str_view_t *paths, size_t count, const char *what) {
    if (!subs_may_leave(app)) return;
    proven_result_mem_mut_t res = rubraview_arena_alloc_array(app->arena, count, sizeof(rubraview_fs_entry_t));
    if (!proven_is_ok(res.err)) return;
    rubraview_fs_entry_t *entries = (rubraview_fs_entry_t*)(void*)res.value.ptr;
    size_t n = 0;
    for (size_t i = 0; i < count; ++i) {
        rubraview_fs_entry_t entry;
        if (!rubraview_pal_fs_stat(app->arena, paths[i], &entry) || entry.is_directory) continue;
        entries[n++] = entry;
    }
    if (n == 0) { osd_say(app, TU8("none of those files could be read")); return; }
    char line[96];
    snprintf(line, sizeof(line), "%zu %s", n, what);
    open_set_from_entries(app, entries, n, cstr(line));
    recent_note_shown(app);
}

bool path_is_listed_kind(proven_u8str_view_t path) {
    char kinds_buf[1024];
    if (is_station_path(path)) return true;   /* RV-157 */
    return rubraview_glob_match_list(rubraview_path_basename(path), kinds_with_text(kinds_buf, sizeof(kinds_buf), IMAGE_FILTER ";" MEDIA_FILTER));
}

/* "Add to the list": the pages being read and the files named, as one
   set. A folder being read becomes a set of its files; a book has no
   files to add to, so the files are opened as a set of their own.

   D-100: the files go to the end of the list (Settings > General > "A
   file added to the list goes": end, or sorted with the rest). `show`
   turns to the first of them — what a file opened from Explorer asks —
   and without it the reader stays on the page being read. */
static void list_add_paths(app_state_t *app, const proven_u8str_view_t *paths, size_t count, bool show) {
    bool files_open = page_count(app) > 0 && app->source.archive_path.size == 0;
    if (!files_open) {
        open_paths_as_set(app, paths, count, count == 1 ? T("file opened as a list") : T("files opened as a list"));
        return;
    }
    bool at_end = lround(rubraview_settings_get(&app->settings, PROVEN_LIT("general"), PROVEN_LIT("added_order"))) == 0;

    size_t have = page_count(app);
    proven_result_mem_mut_t res = rubraview_arena_alloc_array(app->arena, have + count, sizeof(rubraview_fs_entry_t));
    if (!proven_is_ok(res.err)) return;
    rubraview_fs_entry_t *entries = (rubraview_fs_entry_t*)(void*)res.value.ptr;
    size_t n = 0, added = 0;
    for (size_t i = 0; i < have; ++i) {
        if (at_end || is_station_path(app->source.pages[i].path)) {   /* RV-158: a station cannot be asked about */
            /* Kept as they stand: nothing of a file but its name is needed, so none is asked about. */
            entries[n++] = (rubraview_fs_entry_t){ .name = app->source.pages[i].name, .path = app->source.pages[i].path };
        } else if (rubraview_pal_fs_stat(app->arena, app->source.pages[i].path, &entries[n]) && !entries[n].is_directory) {
            n++;
        }
    }
    size_t kept = n;
    proven_u8str_view_t first_named = PROVEN_LIT("");   /* the first of them that is, or now will be, in the list */
    for (size_t i = 0; i < count; ++i) {
        rubraview_fs_entry_t entry;
        if (!rubraview_pal_fs_stat(app->arena, paths[i], &entry) || entry.is_directory) continue;
        if (!path_is_listed_kind(entry.path)) continue;
        bool there = false;
        for (size_t k = 0; k < n && !there; ++k) {
            there = rubraview_path_same(entries[k].path, entry.path);
            if (there && first_named.size == 0) first_named = entries[k].path;
        }
        if (there) continue;
        entry.path = app_keep(app, entry.path);
        entry.name = rubraview_path_basename(entry.path);
        if (first_named.size == 0) first_named = entry.path;
        entries[n++] = entry;
        added++;
    }

    /* Several files opened at once in Explorer arrive one launch after
       another: the first is turned to, the rest only join the list. */
    double now = rubraview_pal_time_now_seconds();
    if (show && now - app->added_shown_at < RUBRAVIEW_SHELLREQ_GATHER_SECONDS) show = false;
    if (show) app->added_shown_at = now;

    if (added == 0) {
        int32_t page = show && first_named.size > 0 ? rubraview_page_source_find(&app->source, first_named) : -1;
        if (page >= 0) {
            if (page != current_page_index(app)) {
                go_to_spread(app, spread_for_chosen_page(app, page));
                update_precache(app);
            }
        } else {
            osd_say(app, TU8("those files are in the list already"));
        }
        return;
    }

    /* The page being read, and where a film or a song on it had got to. */
    int32_t at = current_page_index(app);
    proven_u8str_view_t reading = at >= 0 && (size_t)at < have ? app->source.pages[at].path : PROVEN_LIT("");
    bool playing = !show && app->media != NULL && app->media_page == at;
    double position = playing ? play_position(app) : 0.0;
    bool paused = app->media_paused;

    char line[96];
    snprintf(line, sizeof(line), T("%zu added to the list (%zu in it)"), added, kept + added);
    open_set_ordered(app, entries, n, cstr(line), at_end, show ? first_named : reading);
    if (show) recent_note_shown(app);

    if (playing && app->media && position > 1.0) {
        play_seek(app, position);
        if (paused && !app->media_paused) media_toggle_pause(app);
    }
}

/* RV-158: a station typed into the list window's box joins the list at its end, by its title, and plays. */
static void list_add_station(app_state_t *app, proven_u8str_view_t address, proven_u8str_view_t title) {
    proven_u8str_view_t path = app_keep(app, address);
    proven_u8str_view_t name = title.size > 0 ? app_keep(app, title) : rubraview_stream_display_name(path);
    bool files_open = page_count(app) > 0 && app->source.archive_path.size == 0;
    size_t have = files_open ? page_count(app) : 0;
    for (size_t i = 0; i < have; ++i) {
        if (!proven_u8str_view_eq(app->source.pages[i].path, path)) continue;
        osd_say(app, TU8("that station is in the list already"));
        go_to_spread(app, spread_for_chosen_page(app, (int32_t)i));
        return;
    }
    proven_result_mem_mut_t res = rubraview_arena_alloc_array(app->arena, have + 1, sizeof(rubraview_fs_entry_t));
    if (!proven_is_ok(res.err)) return;
    rubraview_fs_entry_t *entries = (rubraview_fs_entry_t*)(void*)res.value.ptr;
    for (size_t i = 0; i < have; ++i) entries[i] = (rubraview_fs_entry_t){ .name = app->source.pages[i].name, .path = app->source.pages[i].path };
    entries[have] = (rubraview_fs_entry_t){ .name = name, .path = path };
    char line[96];
    snprintf(line, sizeof(line), T("%zu added to the list (%zu in it)"), (size_t)1, have + 1);
    open_set_ordered(app, entries, have + 1, cstr(line), true, path);
}

void address_begin(app_state_t *app) {
    if (app->rename_active) rename_end(app);
    app->rename_edit = rubraview_textedit_make(app->rename_buffer, sizeof(app->rename_buffer));
    rubraview_textedit_set(&app->rename_edit, PROVEN_LIT("http://"));
    app->rename_active = true;
    app->rename_is_address = true;
    app->rename_composing_length = 0;
    rubraview_pal_window_text_input(app->window, true);
    osd_say(app, TU8("a station's address, then a space and its title if you like (Enter adds it, Esc leaves it)"));
}

void address_typed(app_state_t *app) {
    proven_u8str_view_t text = rubraview_textedit_text(&app->rename_edit);
    proven_u8str_view_t address = {0}, title = {0};
    bool ok = rubraview_stream_split_typed(text, &address, &title);
    char a[RUBRAVIEW_STREAM_URL_MAX], t[512];
    snprintf(a, sizeof(a), "%.*s", ok ? (int)address.size : 0, ok ? (const char *)address.ptr : "");
    snprintf(t, sizeof(t), "%.*s", ok ? (int)title.size : 0, ok ? (const char *)title.ptr : "");
    rename_end(app);
    if (!ok) { osd_say(app, TU8("not a station's address: http, https or mms, then ://")); return; }
    list_add_station(app, cstr(a), cstr(t));
}

void panel_open_print(app_state_t *app);
static void print_devices_load(app_state_t *app);
void print_thumbs_load(app_state_t *app);
static void print_thumbs_drop(app_state_t *app);

/* D-87: these pictures are to be printed: how is asked first, in the panel. */
void print_paths(app_state_t *app, const proven_u8str_view_t *paths, size_t count) {
    proven_result_mem_mut_t res = rubraview_arena_alloc_array(app->arena, count ? count : 1, sizeof(proven_u8str_view_t));
    if (!proven_is_ok(res.err) || count == 0) return;
    app->print_list = (proven_u8str_view_t*)(void*)res.value.ptr;
    for (size_t i = 0; i < count; ++i) app->print_list[i] = paths[i];
    app->print_list_count = count;
    if (app->panel.open) panel_close(app);
    app->print_preview_sheet = 0;
    print_devices_load(app);   /* 1.19.0 */
    print_thumbs_load(app);
    panel_open_print(app);
}


/* D-100: a file opened from Explorer — a double click, the menu's Open,
   a launch with files named. With files already being read it joins the
   end of their list and is turned to (owner, 2026-10-11: "현재 재생목록
   맨끝에 이미지가 추가되면서 새 파일로 열리게"); the first file a window is
   given brings its folder ("맨 처음 열릴때만 같은 폴더 파일들을"). Both are
   settings: General > "A file given to the open window" and "The first
   file brings its folder". A book, a folder or a playlist is not a file
   of a list: it is opened in place of what was open. */
static void explorer_open(app_state_t *app, const proven_u8str_view_t *paths, size_t count) {
    enum { GIVEN_ADD = 0, GIVEN_FOLDER, GIVEN_ONLY };
    int given = (int)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("general"), PROVEN_LIT("explorer_open")));
    bool files_open = page_count(app) > 0 && app->source.archive_path.size == 0;
    bool listed = true;
    for (size_t i = 0; i < count && listed; ++i) listed = path_is_listed_kind(paths[i]);

    if (!listed) {
        if (count == 1) open_path(app, paths[0]);
        else open_paths_as_set(app, paths, count, T("files opened as one set"));
        return;
    }
    if (given == GIVEN_ADD && files_open) {
        list_add_paths(app, paths, count,
                       rubraview_settings_get(&app->settings, PROVEN_LIT("general"), PROVEN_LIT("added_shows")) > 0.5);
        return;
    }
    if (count > 1) { open_paths_as_set(app, paths, count, T("files opened as one set")); return; }

    bool with_folder = given == GIVEN_FOLDER ||
                       (given == GIVEN_ADD && rubraview_settings_get(&app->settings, PROVEN_LIT("general"), PROVEN_LIT("first_with_folder")) > 0.5);
    if (with_folder) open_path(app, paths[0]);
    else open_paths_as_set(app, paths, count, T("file opened on its own"));
}

static void shell_request_act(app_state_t *app, rubraview_shell_verb_t verb, const proven_u8str_view_t *paths, size_t count) {
    if (count == 0) return;
    switch (verb) {
        case RUBRAVIEW_SHELL_VERB_NONE:
        case RUBRAVIEW_SHELL_VERB_OPEN:
        case RUBRAVIEW_SHELL_VERB_NEW_WINDOW:   /* its window is this one, just made */
            explorer_open(app, paths, count);
            break;
        case RUBRAVIEW_SHELL_VERB_OPEN_ONLY:
            if (count == 1 && !path_is_listed_kind(paths[0])) open_path(app, paths[0]);   /* a book, a folder */
            else open_paths_as_set(app, paths, count, count == 1 ? T("file opened on its own") : T("files opened as one set"));
            break;
        case RUBRAVIEW_SHELL_VERB_ADD:
            list_add_paths(app, paths, count, false);
            break;
        case RUBRAVIEW_SHELL_VERB_BROWSE:
            open_path(app, paths[0]);
            if (app->source.archive_path.size > 0 && rubraview_path_same(app->source.archive_path, paths[0])) {
                app->picker_dir = PROVEN_LIT("");
                picker_open(app);
            }
            break;
        case RUBRAVIEW_SHELL_VERB_CONVERT: {
            proven_result_mem_mut_t res = rubraview_arena_alloc_array(app->arena, count, sizeof(proven_u8str_view_t));
            if (!proven_is_ok(res.err)) break;
            app->batch_list = (proven_u8str_view_t*)(void*)res.value.ptr;
            for (size_t i = 0; i < count; ++i) app->batch_list[i] = paths[i];
            app->batch_list_count = count;
            batch_window_toggle(app, true);   /* RV-194: these files, in Batch's window */
            break;
        }
        case RUBRAVIEW_SHELL_VERB_PRINT:
            print_paths(app, paths, count);
            break;
        /* RV-207 (D-252): Explorer's Rubraview menu, as the open-file screen's right click */
        case RUBRAVIEW_SHELL_VERB_EXTRACT:
            archwork_extract_paths(app, paths, count, RUBRAVIEW_EXTRACT_FOLDER, false);
            break;
        case RUBRAVIEW_SHELL_VERB_EXTRACT_HERE:
            archwork_extract_paths(app, paths, count, RUBRAVIEW_EXTRACT_SMART, false);
            break;
        case RUBRAVIEW_SHELL_VERB_EXTRACT_TO:
            archwork_extract_paths(app, paths, count, RUBRAVIEW_EXTRACT_FOLDER, true);
            break;
        case RUBRAVIEW_SHELL_VERB_COMPRESS_ZIP:
            archwork_compress_paths(app, paths, count, false);
            break;
        case RUBRAVIEW_SHELL_VERB_COMPRESS:
            archwork_compress_paths(app, paths, count, true);
            break;
        default:
            break;
    }
}

/* What was gathered, acted on: the paths copied where they outlive it. */
void shell_requests_flush(app_state_t *app) {
    rubraview_shellreq_collector_t *c = &app->shell_requests;
    if (!c->pending) return;
    rubraview_shell_verb_t verb = c->verb;
    size_t count = 0;
    proven_u8str_view_t *paths = NULL;
    proven_result_mem_mut_t list = rubraview_arena_alloc_array(app->arena, c->count ? c->count : 1, sizeof(proven_u8str_view_t));
    proven_result_mem_mut_t text = proven_arena_alloc(app->arena, c->text_len + 1);
    if (proven_is_ok(list.err) && proven_is_ok(text.err)) {
        paths = (proven_u8str_view_t*)(void*)list.value.ptr;
        char *copy = (char*)(void*)text.value.ptr;
        memcpy(copy, c->text, c->text_len);
        for (const char *p = rubraview_shellreq_next(c, NULL); p && count < c->count; p = rubraview_shellreq_next(c, p)) {
            paths[count++] = (proven_u8str_view_t){ .ptr = (const proven_byte_t *)copy + (p - c->text), .size = strlen(p) };
        }
    }
    rubraview_shellreq_clear(c);
    if (paths) shell_request_act(app, verb, paths, count);
}

/* One request into the gathering; one with another verb waiting goes first. */
void shell_request_take(app_state_t *app, rubraview_shell_verb_t verb, bool whole, const proven_u8str_view_t *paths, size_t count) {
    double now = rubraview_pal_time_now_seconds();
    if (!rubraview_shellreq_collect(&app->shell_requests, verb, whole, paths, count, now)) {
        shell_requests_flush(app);
        (void)rubraview_shellreq_collect(&app->shell_requests, verb, whole, paths, count, now);
    }
}

/* Once a pass of the main loop: what other launches handed over. */
size_t shell_requests_pump(app_state_t *app) {
    size_t handled = 0;
    char *blob = NULL;
    size_t len = 0;
    while (rubraview_pal_instance_take_request(&blob, &len)) {
        rubraview_shell_verb_t verb = RUBRAVIEW_SHELL_VERB_NONE;
        bool whole = false;
        size_t count = 0;
        if (rubraview_shellreq_unpack(blob, len, &verb, &whole, NULL, 0, &count) && count > 0) {
            proven_u8str_view_t *paths = (proven_u8str_view_t*)rubraview_malloc_product(count, sizeof(proven_u8str_view_t), 1);
            if (paths && rubraview_shellreq_unpack(blob, len, &verb, &whole, paths, count, &count)) {
                /* A plain launch is one thing and is not waited on. */
                shell_request_take(app, verb, whole || verb == RUBRAVIEW_SHELL_VERB_NONE, paths, count);
            }
            free(paths);
        }
        free(blob);
        handled++;
    }
    if (rubraview_shellreq_ready(&app->shell_requests, rubraview_pal_time_now_seconds())) {
        shell_requests_flush(app);
        handled++;
    }
    return handled;
}

void handle_drop(app_state_t *app, const rubraview_window_event_t *event) {
    rubraview_drop_item_t items[16];
    size_t count = event->drop.count < 16 ? event->drop.count : 16;
    if (count == 0) return;   /* nothing was dropped: there is no first item to open */

    /* D-111: let go over the list window, they join the list at that row. */
    if (event->kind == RUBRAVIEW_WINDOW_EVENT_DROP && event->drop.has_point && app->listwin.open && list_editable(app) &&
        !app->picker_open && rubraview_rect_contains(rubraview_listwin_rect(&app->listwin), event->drop.x, event->drop.y)) {
        proven_u8str_view_t paths[16];
        for (size_t i = 0; i < count; ++i) paths[i] = (proven_u8str_view_t){ .ptr = (const proven_byte_t *)event->drop.paths[i], .size = event->drop.path_lengths[i] };
        listwin_sync(app);
        list_insert_paths(app, paths, count, list_viewed(app) ? page_count(app) : rubraview_listwin_insert_index(&app->listwin, event->drop.y, page_count(app)));
        return;
    }

    for (size_t i = 0; i < count; ++i) {
        items[i].path = (proven_u8str_view_t){ .ptr = (const proven_byte_t *)event->drop.paths[i], .size = event->drop.path_lengths[i] };
        /* A directory has no extension and does list: asking the
           filesystem is cheaper than guessing from the name. */
        rubraview_fs_listing_t probe = rubraview_pal_fs_list_dir(app->arena, items[i].path);
        items[i].is_directory = probe.count > 0;
    }

    switch (rubraview_drop_classify(items, count)) {
        case RUBRAVIEW_DROP_OPEN_FILE:
        case RUBRAVIEW_DROP_OPEN_FOLDER:
            open_path(app, items[0].path);
            break;
        case RUBRAVIEW_DROP_PLAYLIST:
            /* §3.19.2: several things dropped together become a sequence
               of exactly those things — not the folder they came from,
               which would show files the reader did not drop. */
            open_dropped_files(app, items, count);
            break;
        case RUBRAVIEW_DROP_NOTHING:
        default:
            break;
    }
}

/* ---- the workbench and the two dialogs (§3.13, §3.10, §3.11) ---- */


/* ---- RV-133: classifying (owner, 2026-10-17) ----
   A window of the nine curation folders. A click on one, or its digit,
   sets the picture on screen aside for it and goes on to the next; Copy
   all or Move all then puts every one in its folder at once, a name never
   replaced (name-1.ext), each move undone with Ctrl+Z (the last 64). */

void classify_build(app_state_t *app) {
    static char words[13][192];
    double dpi = rubraview_pal_window_dpi_scale(app->window);
    bool placed = app->classify_panel.placed;
    double px = app->classify_panel.place_x, py = app->classify_panel.place_y;
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->window, &w, &h);
    app->classify_panel = rubraview_panel_create(TU8("Classify"), dpi);
    app->classify_panel.bounds.width = 360.0 * dpi;
    app->classify_panel.place_x = placed ? px : (double)w - 372.0 * dpi;
    app->classify_panel.place_y = placed ? py : 90.0 * dpi;
    app->classify_panel.placed = true;
    const rubraview_classify_t *c = app->classify;
    proven_u8str_view_t here = current_file_path(app);
    int32_t mark = c ? rubraview_classify_folder_of(c, here) : 0;
    int n;
    size_t k = 0;
    bool any = false;
    for (int32_t d = 1; d <= 9; ++d) {
        proven_u8str_view_t dir = rubraview_curation_target(&app->curation, d);
        if (dir.size == 0) continue;
        any = true;
        proven_u8str_view_t name = rubraview_path_basename(dir);
        n = snprintf(words[k], sizeof(words[k]), "%s%d  %.*s  (%zu)", d == mark ? "> " : "  ", (int)d,
                     (int)(name.size < 120 ? name.size : 120), name.ptr, c ? rubraview_classify_count(c, d) : (size_t)0);
        rubraview_panel_add_button(&app->classify_panel, PANEL_CLS_FOLDER + d, (proven_u8str_view_t){ (const proven_byte_t *)words[k], n > 0 ? (size_t)n : 0 });
        ++k;
    }
    if (!any) rubraview_panel_add_button(&app->classify_panel, PANEL_CLS_SETTINGS, TU8("No folders yet: set them in Settings > Files..."));
    rubraview_panel_add_separator(&app->classify_panel);
    if (mark) {
        n = snprintf(words[10], sizeof(words[10]), T("This one goes to %d: press to take it off"), (int)mark);
        rubraview_panel_add_button(&app->classify_panel, PANEL_CLS_HERE, (proven_u8str_view_t){ (const proven_byte_t *)words[10], n > 0 ? (size_t)n : 0 });
    } else {
        rubraview_panel_add_button(&app->classify_panel, PANEL_CLS_HERE, TU8("This one is not set aside (a folder or its digit)"));
    }
    size_t all = c ? rubraview_classify_count(c, 0) : 0;
    n = snprintf(words[11], sizeof(words[11]), T("Copy all set aside (%zu)"), all);
    rubraview_panel_add_button(&app->classify_panel, PANEL_CLS_COPY, (proven_u8str_view_t){ (const proven_byte_t *)words[11], n > 0 ? (size_t)n : 0 });
    n = snprintf(words[12], sizeof(words[12]), T("Move all set aside (%zu)"), all);
    rubraview_panel_add_button(&app->classify_panel, PANEL_CLS_MOVE, (proven_u8str_view_t){ (const proven_byte_t *)words[12], n > 0 ? (size_t)n : 0 });
    rubraview_panel_add_button(&app->classify_panel, PANEL_CLS_CLEAR, TU8("Forget what is set aside"));
    if (any) rubraview_panel_add_button(&app->classify_panel, PANEL_CLS_SETTINGS, TU8("The folders: Settings > Files..."));
    app->classify_panel.open = true;
    rubraview_panel_layout(&app->classify_panel, (double)w, (double)h);
    app->classify_shown_page = current_page_index(app);
}

void classify_toggle(app_state_t *app) {
    if (app->classify_panel.open) { app->classify_panel.open = false; return; }
    if (!app->classify) {
        app->classify = (rubraview_classify_t*)calloc(1, sizeof(*app->classify));
        if (!app->classify) { osd_say(app, TU8("out of memory")); return; }
    }
    classify_build(app);
}

void classify_mark(app_state_t *app, int32_t digit) {
    if (!app->classify) return;
    if (app->source.kind != RUBRAVIEW_PAGE_SOURCE_FOLDER) { osd_say(app, TU8("a page inside an archive is not a file of its own: it cannot be set aside")); return; }
    proven_u8str_view_t path = current_file_path(app);
    if (path.size == 0) return;
    int32_t was = rubraview_classify_folder_of(app->classify, path);
    if (!rubraview_classify_set(app->classify, was ? path : app_keep(app, path), digit)) {
        osd_say(app, TU8("the list is full: copy or move what is set aside first"));
        return;
    }
    char said[96];
    int n = snprintf(said, sizeof(said), T("set aside for %d (%zu in all)"), (int)digit, rubraview_classify_count(app->classify, 0));
    osd_say(app, (proven_u8str_view_t){ (const proven_byte_t *)said, n > 0 ? (size_t)n : 0 });
    if ((size_t)current_page_index(app) + 1 < page_count(app)) handle_action(app, PROVEN_LIT("next_page"));
    classify_build(app);
}

static void classify_run(app_state_t *app, bool moving) {
    rubraview_classify_t *c = app->classify;
    if (!c || c->count == 0) { osd_say(app, TU8("nothing is set aside")); return; }
    size_t done = 0, failed = 0;
    for (size_t i = 0; i < c->count;) {
        proven_u8str_view_t path = c->paths[i];
        proven_u8str_view_t dir = rubraview_curation_target(&app->curation, c->folders[i]);
        bool ok = false;
        if (dir.size > 0 && (rubraview_pal_fs_make_dirs(dir) || rubraview_pal_fs_exists(dir))) {
            if (moving && rubraview_path_same(rubraview_path_dirname(path), dir)) ok = true;   /* already there */
            else {
                proven_u8str_view_t target = rubraview_filing_target(app->arena, dir, rubraview_path_basename(path), filing_exists, NULL);
                ok = target.size > 0 && (moving ? rubraview_pal_fs_move(path, target) : rubraview_pal_fs_copy(path, target));
                if (ok) rubraview_undo_push(&app->undo, (rubraview_file_action_t){
                    .op = moving ? RUBRAVIEW_FILE_OP_MOVE : RUBRAVIEW_FILE_OP_COPY,
                    .source_path = path, .target_path = target, .playlist_index = 0 });
            }
        }
        if (ok) { rubraview_classify_remove_at(c, i); ++done; }
        else { ++failed; ++i; }   /* kept, still set aside */
    }
    char said[160];
    int n = failed ? snprintf(said, sizeof(said), moving ? T("moved %zu; %zu could not be put there and are still set aside") : T("copied %zu; %zu could not be put there and are still set aside"), done, failed)
                   : snprintf(said, sizeof(said), moving ? T("moved %zu") : T("copied %zu"), done);
    osd_say(app, (proven_u8str_view_t){ (const proven_byte_t *)said, n > 0 ? (size_t)n : 0 });
    if (moving && done > 0) {
        int32_t page = current_page_index(app);
        reopen_after_removal(app, page > 0 ? (size_t)page : 0);
    }
    classify_build(app);
}

bool classify_press(app_state_t *app, double px, double py) {
    rubraview_panel_t *box = &app->classify_panel;
    if (!box->open || !rubraview_rect_contains(box->bounds, px, py)) return false;
    if (scrollbar_press_panel(app, box, px, py)) return true;   /* RV-172 */
    if (rubraview_panel_title_hit(box, px, py)) { rubraview_panel_move_begin(box, px, py); return true; }
    int32_t row = -1;
    rubraview_panel_event_t ev = rubraview_panel_press(box, px, py, &row);
    if (ev == RUBRAVIEW_PANEL_CLOSE) { box->open = false; return true; }
    if (ev != RUBRAVIEW_PANEL_BUTTON_PRESSED || row < 0) return true;
    int32_t id = box->rows[row].id;
    if (id > PANEL_CLS_FOLDER && id <= PANEL_CLS_FOLDER + 9) { classify_mark(app, id - PANEL_CLS_FOLDER); return true; }
    switch (id) {
        case PANEL_CLS_HERE:
            if (app->classify && rubraview_classify_set(app->classify, current_file_path(app), 0)) classify_build(app);
            break;
        case PANEL_CLS_COPY:  classify_run(app, false); break;
        case PANEL_CLS_MOVE:  classify_run(app, true); break;
        case PANEL_CLS_CLEAR:
            if (app->classify) rubraview_classify_clear(app->classify);
            osd_say(app, TU8("nothing is set aside now"));
            classify_build(app);
            break;
        case PANEL_CLS_SETTINGS: settings_open(app); break;
        default: break;
    }
    return true;
}


/* The panel is placed against the window, so it is placed again whenever
   the window changes size. */
void panel_relayout(app_state_t *app) {
    if (!app->panel.open) return;
    if (owned_panel_relayout(app)) return;   /* RV-194: Batch's or its names' window */
    if (app->edit_window && edit_panel_open(app)) { edit_window_layout(app); return; }   /* RV-137: laid out in its own window */
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->window, &w, &h);
    if (w > 0 && h > 0) rubraview_panel_layout(&app->panel, (double)w, (double)h);
}

void edit_preview_close(app_state_t *app);
void edit_preview_open(app_state_t *app);
void media_reopen_on_new_device(app_state_t *app);

void arc_ask_clear(app_state_t *app);
void print_temp_drop(app_state_t *app);

void px_edit_close(app_state_t *app);

/* RV-137 (owner, 2026-10-18: "선택 영역 곧바로 잘라서 클립보드에 복사"): the picture at
   its own size with the changes in force, cut to the chosen area (an ellipse's
   corners left clear), put on the clipboard; no area, the whole picture. */
void edit_copy_area(app_state_t *app) {
    int32_t page = current_page_index(app);
    if (page < 0 || (size_t)page >= page_count(app) || !app->pages[page].loaded) { osd_say(app, TU8("there is no picture to copy")); return; }
    int32_t w = real_w(&app->pages[page]), h = real_h(&app->pages[page]);
    if (w <= 0 || h <= 0 || (uint64_t)w * (uint64_t)h > (uint64_t)100000000u) { osd_say(app, TU8("this picture is too large to copy")); return; }
    rubraview_edit_steps_t steps = app->edit_steps;
    steps.count = steps.cursor;   /* the changes in force, the one in hand among them */
    /* RV-141 (4): the picture read into memory of its own, then each step from one lane into
       the other, the one before given back as the next is made — not a picture's size a step. */
    size_t pixels = 0, first = 0, biggest = 0;
    if (!rubraview_size_product(&pixels, (size_t)w, (size_t)h, 4u)) { osd_say(app, TU8("this picture is too large to copy")); return; }
    biggest = pixels;
    for (size_t k = 1; k <= steps.count; ++k) {
        int32_t sw = 0, sh = 0;
        size_t b = 0;
        rubraview_edit_steps_size(&steps, k, &sw, &sh);
        if (sw > 0 && sh > 0 && rubraview_size_product(&b, (size_t)sw, (size_t)sh, 4u) && b > biggest) biggest = b;
    }
    size_t lane = 0, big = 0;
    if (!rubraview_size_product(&first, pixels, 3u, 1u) || !rubraview_size_product(&lane, biggest, 6u, 1u) ||
        PROVEN_CKD_ADD(&first, first, (size_t)(16u << 20)) || PROVEN_CKD_ADD(&lane, lane, (size_t)(16u << 20)) ||
        PROVEN_CKD_ADD(&big, first, lane) || PROVEN_CKD_ADD(&big, big, lane)) { osd_say(app, TU8("this picture is too large to copy")); return; }
    void *memory = malloc(big);
    if (!memory) { osd_say(app, TU8("there is not memory enough to copy this picture")); return; }
    proven_arena_t scratch = proven_arena_create((proven_mem_mut_t){ .ptr = (proven_byte_t*)memory, .size = first });
    proven_arena_t lanes[2] = {
        proven_arena_create((proven_mem_mut_t){ .ptr = (proven_byte_t*)memory + first, .size = lane }),
        proven_arena_create((proven_mem_mut_t){ .ptr = (proven_byte_t*)memory + first + lane, .size = lane }),
    };
    rubraview_pixbuf_t full = {0};
    proven_u8str_view_t path = app->source.pages[page].path;
    if (path.size > 0) {
        full = rubraview_pal_image_read_pixels_within(&scratch, path, NULL, 0, true, 0, 0);
    } else {
        rubraview_page_bytes_t bytes = { .ok = false };
        AcquireSRWLockExclusive(&app->source_lock);
        bytes = rubraview_page_source_read(&scratch, &app->source, (size_t)page, MAX_PAGE_BYTES);
        ReleaseSRWLockExclusive(&app->source_lock);
        if (bytes.ok && bytes.data.size > 0)
            full = rubraview_pal_image_read_pixels_within(&scratch, PROVEN_LIT(""), (const uint8_t*)bytes.data.ptr, bytes.data.size, true, 0, 0);
    }
    rubraview_pixbuf_t done = full;
    for (size_t k = 0; k < steps.count && rubraview_pixbuf_is_valid(&done); ++k) {
        proven_arena_t *into = &lanes[k % 2u];
        proven_arena_reset(into);
        rubraview_pixbuf_t next = rubraview_edit_step_apply(into, &steps.steps[k], &done, 1.0);
        if (rubraview_pixbuf_is_valid(&next)) done = next; else done = (rubraview_pixbuf_t){0};
    }
    proven_arena_t *out_arena = &lanes[steps.count % 2u];   /* the lane not holding the result */
    proven_arena_reset(out_arena);
    if (steps.count == 0) out_arena = &lanes[0];
    rubraview_pixbuf_t bgra = !rubraview_pixbuf_is_valid(&done) || done.format == RUBRAVIEW_PIXFMT_BGRA8
        ? done : rubraview_pixbuf_convert(out_arena, &done, RUBRAVIEW_PIXFMT_BGRA8);
    const rubraview_crop_rect_t *c = &app->edit.crop;
    if (rubraview_pixbuf_is_valid(&bgra) && app->edit.crop_active && c->width >= 1 && c->height >= 1 &&
        c->x + c->width <= bgra.width && c->y + c->height <= bgra.height) {
        rubraview_pixbuf_t cut = rubraview_pixbuf_crop(bgra.pixels == done.pixels ? out_arena : &scratch, &bgra, c->x, c->y, c->width, c->height);
        if (rubraview_pixbuf_is_valid(&cut) && app->sel_shape == RUBRAVIEW_AREA_ELLIPSE) {
            double rx = cut.width / 2.0, ry = cut.height / 2.0;
            for (int32_t y = 0; y < cut.height; ++y) {
                uint8_t *row = cut.pixels + (size_t)y * (size_t)cut.stride;
                for (int32_t x = 0; x < cut.width; ++x) {
                    double dx = (x + 0.5 - rx) / rx, dy = (y + 0.5 - ry) / ry;
                    if (dx * dx + dy * dy > 1.0) row[x * 4 + 3] = 0;   /* outside the ellipse: clear */
                }
            }
        }
        bgra = cut;
    }
    bool copied = rubraview_pixbuf_is_valid(&bgra) &&
                  rubraview_pal_clipboard_set_image(app->window, bgra.pixels, bgra.width, bgra.height, (size_t)bgra.stride);
    free(memory);
    osd_say(app, !copied ? TU8("the picture could not be copied")
                 : app->edit.crop_active ? TU8("the area is on the clipboard") : TU8("the picture is on the clipboard"));
}

void sel_mode_set(app_state_t *app, bool on) {
    app->sel_mode = on;
    rubraview_pal_window_set_cursor_shape(app->window, on ? RUBRAVIEW_CURSOR_CROSS : RUBRAVIEW_CURSOR_ARROW);
}

void panel_close(app_state_t *app) {
    bool back_to_edit = app->export_from_edit && app->panel_is_export;
    app->export_from_edit = false;
    sel_mode_set(app, false);
    edit_window_close(app);
    edit_preview_close(app);
    app->sel_panel.open = false;
    app->steps_panel.open = false;
    app->sel_grab = 0;
    if (app->px_edit_open) px_edit_close(app);
    app->panel.open = false;
    app->panel.row_count = 0;
    app->panel_is_export = false;
    app->panel_is_batch = false;
    /* D-89: closed without its button, the question is answered "no". */
    if (app->panel_is_convert && app->arc_ask == ARC_ASK_FORMAT) {
        arc_ask_clear(app);
        osd_say(app, TU8("the archive is as it was"));
    }
    app->panel_is_convert = false;
    if (app->panel_is_print && !app->print_going) print_temp_drop(app);
    if (app->panel_is_print) print_thumbs_drop(app);   /* 1.19.0 */
    app->panel_is_print = false;
    if (back_to_edit) {   /* Export done or left: Edit again, with its edits */
        app->edit_kept = true;
        panel_open_edit(app);
    }
    toolbox_refresh(app);    /* Edit closed: the toolbox whole again */
}

/* §3.13's workbench. Every row's range comes from the edit session, so
   the panel cannot offer a value the commit layer would refuse. */
/* RV-108: the panel has two pages — the tones, and sharpness, cut and
   size — so that it is never taller than a small window. The rows are
   made from the session as it stands: turning the page changes nothing. */
bool g_edit_page_two = false;

/* The size Save makes, in pixels, on the button that types it (owner, 2026-10-15). */
proven_u8str_view_t px_button_label(const app_state_t *app) {
    static char label[64];
    const rubraview_edit_session_t *e = &app->edit;
    int32_t w = e->resize_width, h = e->resize_height;
    if (w <= 0 || h <= 0) { w = e->image_width; h = e->image_height; }   /* the whole picture is resized (the owner's edit) */
    /* RV-128: and what it prints at, at the DPI below */
    double dpi = e->resize_dpi > 0.0 ? e->resize_dpi : 300.0;
    int n = snprintf(label, sizeof(label), T("Size: %d x %d px (%.1f x %.1f cm)..."), (int)w, (int)h, w / dpi * 2.54, h / dpi * 2.54);
    return n > 0 ? (proven_u8str_view_t){ .ptr = (const proven_byte_t *)label, .size = (size_t)n } : TU8("Size in pixels...");
}

/* The cut on the button that types it (owner, 2026-10-15). */
proven_u8str_view_t crop_button_label(const app_state_t *app) {
    static char label[64];
    const rubraview_edit_session_t *e = &app->edit;
    if (!e->crop_active) return TU8("Crop: type it, or drag on the picture...");
    int n = snprintf(label, sizeof(label), T("Crop: %d, %d  %d x %d px..."), (int)e->crop.x, (int)e->crop.y, (int)e->crop.width, (int)e->crop.height);
    return n > 0 ? (proven_u8str_view_t){ .ptr = (const proven_byte_t *)label, .size = (size_t)n } : TU8("Crop...");
}

/* The cut changed (a drag, Trim, the ratio): the buttons and the box say it again. */
void crop_shown_again(app_state_t *app) {
    if (app->panel.open && !app->panel_is_export && !app->panel_is_batch && !app->panel_is_print && !app->panel_is_convert) {
        int32_t k = rubraview_panel_find(&app->panel, PANEL_CROP_PX);
        if (k >= 0) app->panel.rows[k].label = crop_button_label(app);
        k = rubraview_panel_find(&app->panel, PANEL_RESIZE_PX);
        if (k >= 0) app->panel.rows[k].label = px_button_label(app);
    }
    if (app->px_edit_open && app->px_edit_crop) px_edit_fill_crop(app);
}

/* D-87: how the pictures are printed, chosen when printing. The choices
   are settings (`print.*`), so the next print starts from the last. */
static rubraview_print_options_t print_options(const app_state_t *app) {
    rubraview_print_options_t o = {
        .turn = (rubraview_print_turn_t)(int)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("print"), PROVEN_LIT("turn"))),
        .size = (rubraview_print_size_t)(int)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("print"), PROVEN_LIT("size"))),
        .scale_percent = (int32_t)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("print"), PROVEN_LIT("scale"))),
        .place = (rubraview_print_place_t)(int)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("print"), PROVEN_LIT("place"))),
        .margin_mm = (int32_t)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("print"), PROVEN_LIT("margin_mm"))),
        .per_sheet = rubraview_print_per_sheet_of((int32_t)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("print"), PROVEN_LIT("per_sheet")))),
    };
    rubraview_print_options_clamp(&o);
    return o;
}

/* ---- 1.19.0 (owner, 2026-10-18: "인쇄할 때 용지 선택 가능하게 하고 프리뷰로 어떻게 인쇄되는지 예상 그림
   보여주세요. 프린터 선택도 가능해야 해요."): the printer, its paper and the way up in the panel, a preview of
   the sheet beside it ---- */

static const app_state_t *g_print_app;   /* whose printers and papers the row words name */

proven_u8str_view_t print_printer_name(const app_state_t *app) {
    if (app->print_printer < 0 || (size_t)app->print_printer >= app->print_printer_count) return PROVEN_LIT("");
    return cstr(app->print_printer_names[app->print_printer]);
}

static int32_t print_orientation(const app_state_t *app) {
    return (int32_t)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("print"), PROVEN_LIT("orientation")));
}

static void print_thumbs_drop(app_state_t *app) {
    for (size_t i = 0; i < 9; ++i) {
        if (app->print_thumbs[i]) rubraview_pal_texture_destroy(app->print_thumbs[i]);
        app->print_thumbs[i] = NULL;
    }
    app->print_thumb_count = 0;
}

/* The pictures of the sheet the preview shows, small, white under what is see-through. */
void print_thumbs_load(app_state_t *app) {
    print_thumbs_drop(app);
    rubraview_print_options_t o = print_options(app);
    size_t sheets = rubraview_print_sheets(app->print_list_count, o.per_sheet);
    if (app->print_preview_sheet < 0 || (size_t)app->print_preview_sheet >= sheets) app->print_preview_sheet = 0;
    size_t first = (size_t)app->print_preview_sheet * (size_t)o.per_sheet;
    rubraview_scratch_t sc;
    proven_arena_t *sa = scratch_begin(app, &sc, 16u << 20);
    for (size_t k = 0; k < (size_t)o.per_sheet && first + k < app->print_list_count && k < 9; ++k) {
        proven_arena_reset(sa);
        rubraview_pixbuf_t px = rubraview_pal_image_read_pixels_within(sa, app->print_list[first + k], NULL, 0, true, 720, 720);
        if (!px.pixels || px.width <= 0 || px.height <= 0 || px.stride != px.width * 4) continue;
        bool rgba = px.format == RUBRAVIEW_PIXFMT_RGBA8;
        uint8_t *p = px.pixels;
        for (size_t i = 0; i < (size_t)px.width * (size_t)px.height; ++i, p += 4) {
            unsigned a = p[3], rest = 255u - a;
            uint8_t c0 = (uint8_t)((p[0] * a + 255u * rest + 127u) / 255u), c1 = (uint8_t)((p[1] * a + 255u * rest + 127u) / 255u),
                    c2 = (uint8_t)((p[2] * a + 255u * rest + 127u) / 255u);
            p[0] = rgba ? c2 : c0; p[1] = c1; p[2] = rgba ? c0 : c2; p[3] = 255;
        }
        rubraview_texture_t *t = rubraview_pal_texture_create_bgra(app->renderer, px.width, px.height);
        if (t && rubraview_pal_texture_upload_bgra(t, px.pixels, px.stride)) {
            app->print_thumbs[k] = t;
            app->print_thumb_w[k] = px.width;
            app->print_thumb_h[k] = px.height;
        } else if (t) {
            rubraview_pal_texture_destroy(t);
        }
    }
    app->print_thumb_count = 9;
    scratch_end(app, &sc);
}

void print_sheet_load(app_state_t *app) {
    int32_t paper = app->print_paper >= 0 && (size_t)app->print_paper < app->print_paper_count ? app->print_papers[app->print_paper].id : 0;
    app->print_sheet_ok = app->print_printer >= 0 &&
                          rubraview_pal_printer_sheet(print_printer_name(app), paper, print_orientation(app), &app->print_sheet);
}

void print_papers_load(app_state_t *app) {
    app->print_paper_count = 0;
    app->print_paper = -1;
    if (app->print_printer < 0) return;
    int32_t own = 0;
    app->print_paper_count = rubraview_pal_printer_papers(print_printer_name(app), app->print_papers, 160, &own);
    int32_t want = (int32_t)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("print"), PROVEN_LIT("paper")));
    for (size_t i = 0; i < app->print_paper_count; ++i) if (app->print_papers[i].id == want) app->print_paper = (int32_t)i;
    for (size_t i = 0; i < app->print_paper_count && app->print_paper < 0; ++i) if (app->print_papers[i].id == own) app->print_paper = (int32_t)i;
}

static void print_devices_load(app_state_t *app) {
    g_print_app = app;
    rubraview_scratch_t sc;
    proven_arena_t *sa = scratch_begin(app, &sc, 1u << 20);
    proven_u8str_view_t names[32];
    int32_t def = -1;
    size_t n = rubraview_pal_printers(sa, names, 32, &def);
    app->print_printer_count = 0;
    for (size_t i = 0; i < n; ++i) {
        if (names[i].size >= sizeof(app->print_printer_names[0])) continue;
        memcpy(app->print_printer_names[app->print_printer_count], names[i].ptr, names[i].size);
        app->print_printer_names[app->print_printer_count][names[i].size] = '\0';
        if ((int32_t)i == def) def = (int32_t)app->print_printer_count;
        app->print_printer_count++;
    }
    scratch_end(app, &sc);
    proven_u8str_view_t want = rubraview_settings_get_text(&app->settings, PROVEN_LIT("print"), PROVEN_LIT("printer"));
    app->print_printer = def >= 0 && (size_t)def < app->print_printer_count ? def : (app->print_printer_count > 0 ? 0 : -1);
    for (size_t i = 0; i < app->print_printer_count; ++i)
        if (want.size > 0 && proven_u8str_view_eq(want, cstr(app->print_printer_names[i]))) app->print_printer = (int32_t)i;
    print_papers_load(app);
    print_sheet_load(app);
}

/* The sheet as it will come out, to the left of the panel: the paper, the part the printer reaches,
   the cells, and each picture where and how it goes — the same sums the printing does. */
void draw_print_preview(app_state_t *app) {
    if (!app->panel.open || !app->panel_is_print) return;
    int32_t win_w = 0, win_h = 0;
    rubraview_pal_window_get_size(app->window, &win_w, &win_h);
    double dpi = rubraview_pal_window_dpi_scale(app->window), pad = 24.0 * dpi;
    double right = app->panel.bounds.x > (double)win_w * 0.3 ? app->panel.bounds.x : (double)win_w;
    rubraview_pal_render_fill_rect(app->renderer, (rubraview_pal_rect_t){ 0, 0, (double)win_w, (double)win_h }, 0xE0202020u, 0.0);
    if (!app->print_sheet_ok) {
        draw_outlined_text_aligned(app->renderer, app->print_printer < 0 ? TU8("no printer: Windows' own dialog prints")
                                                                        : TU8("the printer does not say its sheet"),
                                   (rubraview_pal_rect_t){ pad, pad, right - 2.0 * pad, 40.0 * dpi }, 16.0 * dpi, 2.0, 0xFFFFFFFFu, RUBRAVIEW_TEXT_LEFT);
        return;
    }
    const rubraview_print_sheet_t *sh = &app->print_sheet;
    double scale = 0, px = 0, py = 0;
    if (!rubraview_print_preview_fit(sh, pad, pad + 28.0 * dpi, right - 2.0 * pad, (double)win_h - 2.0 * pad - 28.0 * dpi, &scale, &px, &py)) return;
    rubraview_pal_render_fill_rect(app->renderer, (rubraview_pal_rect_t){ px + 4.0 * dpi, py + 4.0 * dpi, sh->paper_w * scale, sh->paper_h * scale }, 0x80000000u, 0.0);
    rubraview_pal_render_fill_rect(app->renderer, (rubraview_pal_rect_t){ px, py, sh->paper_w * scale, sh->paper_h * scale }, 0xFFFFFFFFu, 0.0);
    double ox = px + sh->print_x * scale, oy = py + sh->print_y * scale;
    rubraview_pal_render_stroke_rect(app->renderer, (rubraview_pal_rect_t){ ox, oy, sh->print_w * scale, sh->print_h * scale }, 0xFFB0B0B0u, 1.0, 0.0);
    rubraview_print_options_t o = print_options(app);
    for (int32_t k = 0; k < o.per_sheet && k < 9; ++k) {
        int32_t cx = 0, cy = 0, cw = 0, ch = 0;
        rubraview_print_cell(sh->print_w, sh->print_h, sh->dpi_x, sh->dpi_y, &o, k, &cx, &cy, &cw, &ch);
        if (o.per_sheet > 1)
            rubraview_pal_render_stroke_rect(app->renderer, (rubraview_pal_rect_t){ ox + cx * scale, oy + cy * scale, cw * scale, ch * scale }, 0xFFD8D8D8u, 1.0, 0.0);
        if ((size_t)k >= app->print_thumb_count || !app->print_thumbs[k]) continue;
        /* the picture's full size, so the sums are the printing's; the small texture stands for it */
        size_t index = (size_t)app->print_preview_sheet * (size_t)o.per_sheet + (size_t)k;
        int32_t w = 0, h = 0;
        if (index >= app->print_list_count || !rubraview_pal_image_size(app->print_list[index], &w, &h) || w <= 0 || h <= 0) {
            w = app->print_thumb_w[k]; h = app->print_thumb_h[k];
        }
        rubraview_print_placement_t pl;
        if (!rubraview_print_place(cx, cy, cw, ch, sh->dpi_x, sh->dpi_y, w, h, &o, &pl)) continue;
        /* 1.23.1: the part printed, drawn from its own corner (the renderer's region) onto where it goes;
           what is past the cell is cut away as the printing cuts it, so nothing leaves the paper */
        double r[4], mm[6];
        if (!rubraview_print_preview_region(&pl, w, h, app->print_thumb_w[k], app->print_thumb_h[k],
                                            ox + pl.dst_x * scale, oy + pl.dst_y * scale, pl.dst_w * scale, pl.dst_h * scale, r, mm)) continue;
        rubraview_src_rect_t src = { .left = r[0], .top = r[1], .right = r[2], .bottom = r[3] };
        rubraview_mat3x2_t m = { mm[0], mm[1], mm[2], mm[3], mm[4], mm[5] };
        rubraview_pal_render_draw_texture_region(app->renderer, app->print_thumbs[k], src, m, RUBRAVIEW_INTERP_LINEAR);
    }
    char line[200];
    size_t sheets = rubraview_print_sheets(app->print_list_count, o.per_sheet);
    double mm_w = sh->paper_w * 25.4 / sh->dpi_x, mm_h = sh->paper_h * 25.4 / sh->dpi_y;
    int n = snprintf(line, sizeof(line), T("sheet %d of %zu  -  %.0f x %.0f mm"), (int)app->print_preview_sheet + 1, sheets, mm_w, mm_h);
    if (n > 0) draw_outlined_text_aligned(app->renderer, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n },
                                          (rubraview_pal_rect_t){ pad, pad - 4.0 * dpi, right - 2.0 * pad, 28.0 * dpi }, 15.0 * dpi, 2.0, 0xFFFFFFFFu, RUBRAVIEW_TEXT_LEFT);
}

void panel_open_print(app_state_t *app) {
    double dpi = rubraview_pal_window_dpi_scale(app->window);
    app->panel = rubraview_panel_create(TU8("Print"), dpi);
    app->panel_is_export = false;
    app->panel_is_batch = false;
    app->panel_is_print = true;

    rubraview_print_options_t o = print_options(app);
    if (app->print_printer_count > 0) {   /* 1.19.0 */
        rubraview_panel_add_choice(&app->panel, PANEL_PRINT_PRINTER, TU8("Printer"), app->print_printer, (int32_t)app->print_printer_count);
        if (app->print_paper_count > 0)
            rubraview_panel_add_choice(&app->panel, PANEL_PRINT_PAPER, TU8("Paper"), app->print_paper < 0 ? 0 : app->print_paper, (int32_t)app->print_paper_count);
        rubraview_panel_add_choice(&app->panel, PANEL_PRINT_ORIENT, TU8("The sheet"), print_orientation(app), 3);
        rubraview_panel_add_slider(&app->panel, PANEL_PRINT_COPIES, TU8("Copies"),
                                   rubraview_settings_get(&app->settings, PROVEN_LIT("print"), PROVEN_LIT("copies")), 1.0, 99.0, 1.0);
        rubraview_panel_add_separator(&app->panel);
    }
    rubraview_panel_add_choice(&app->panel, PANEL_PRINT_TURN, TU8("Turn"), (int32_t)o.turn, RUBRAVIEW_PRINT_TURN_COUNT);
    rubraview_panel_add_choice(&app->panel, PANEL_PRINT_SIZE, TU8("Size"), (int32_t)o.size, RUBRAVIEW_PRINT_SIZE_COUNT);
    rubraview_panel_add_slider(&app->panel, PANEL_PRINT_SCALE, TU8("Scale %"), (double)o.scale_percent,
                               (double)RUBRAVIEW_PRINT_SCALE_MIN, (double)RUBRAVIEW_PRINT_SCALE_MAX, 5.0);
    rubraview_panel_add_choice(&app->panel, PANEL_PRINT_PLACE, TU8("Place"), (int32_t)o.place, RUBRAVIEW_PRINT_PLACE_COUNT);
    rubraview_panel_add_slider(&app->panel, PANEL_PRINT_MARGIN, TU8("Margin mm"), (double)o.margin_mm,
                               0.0, (double)RUBRAVIEW_PRINT_MARGIN_MAX_MM, 1.0);
    rubraview_panel_add_choice(&app->panel, PANEL_PRINT_PER_SHEET, TU8("A sheet"),
                               rubraview_print_per_sheet_index(o.per_sheet), 5);
    size_t sheets = rubraview_print_sheets(app->print_list_count, o.per_sheet);
    if (sheets > 1) rubraview_panel_add_choice(&app->panel, PANEL_PRINT_SHEET, TU8("Preview of"), app->print_preview_sheet, (int32_t)(sheets < 999 ? sheets : 999));
    rubraview_panel_add_separator(&app->panel);
    static char label[64];
    if (app->print_printer_count == 0) {
        if (app->print_list_count == 1) snprintf(label, sizeof(label), T("Print..."));
        else snprintf(label, sizeof(label), T("Print the %zu chosen files..."), app->print_list_count);
    } else if (app->print_list_count == 1) {
        snprintf(label, sizeof(label), T("Print"));
    } else {
        snprintf(label, sizeof(label), T("Print the %zu chosen files"), app->print_list_count);
    }
    rubraview_panel_add_button(&app->panel, PANEL_PRINT_NOW, cstr(label));
    if (app->print_printer_count > 0) rubraview_panel_add_button(&app->panel, PANEL_PRINT_DIALOG, TU8("With Windows' print dialog..."));

    app->panel.open = true;
    panel_relayout(app);
}

bool g_conv_is_7z;   /* D-89: which format the convert panel's method row is named for */

/* What a row of the print panel shows beside its name; empty for the others. */
proven_u8str_view_t print_row_text(const rubraview_panel_row_t *row, char *buffer, size_t size) {
    int n = 0;
    int32_t v = (int32_t)lround(row->value);
    switch (row->id) {
        case PANEL_PRINT_TURN:      n = snprintf(buffer, size, "%s", rubraview_print_turn_name((rubraview_print_turn_t)v)); break;
        case PANEL_PRINT_SIZE:      n = snprintf(buffer, size, "%s", rubraview_print_size_name((rubraview_print_size_t)v)); break;
        case PANEL_PRINT_PLACE:     n = snprintf(buffer, size, "%s", rubraview_print_place_name((rubraview_print_place_t)v)); break;
        case PANEL_PRINT_PER_SHEET: n = snprintf(buffer, size, "%d", (int)rubraview_print_per_sheet_of(v)); break;
        case PANEL_PRINT_PRINTER:   n = g_print_app && v >= 0 && (size_t)v < g_print_app->print_printer_count
                                          ? snprintf(buffer, size, "%s", g_print_app->print_printer_names[v]) : 0; break;
        case PANEL_PRINT_PAPER:
            if (g_print_app && v >= 0 && (size_t)v < g_print_app->print_paper_count) {
                const rubraview_printer_paper_t *p = &g_print_app->print_papers[v];
                n = snprintf(buffer, size, "%s (%d x %d mm)", p->name, (int)lround(p->w_tenth_mm / 10.0), (int)lround(p->h_tenth_mm / 10.0));
            }
            break;
        case PANEL_PRINT_ORIENT:    n = snprintf(buffer, size, "%s", v == 1 ? T("upright (portrait)") : v == 2 ? T("on its side (landscape)") : T("as the printer has it")); break;
        case PANEL_PRINT_COPIES:    n = snprintf(buffer, size, "%d", (int)v); break;
        case PANEL_PRINT_SHEET:     n = snprintf(buffer, size, T("sheet %d"), (int)v + 1); break;
        case PANEL_PRINT_SCALE:
        case PANEL_PRINT_MARGIN:
        case PANEL_CONV_LEVEL:      n = snprintf(buffer, size, "%d", (int)v); break;
        case PANEL_CONV_FORMAT:     n = snprintf(buffer, size, "%s", v == 1 ? T("7z (CB7)") : T("ZIP (CBZ)")); break;
        /* The words follow the format on the row above: each has one way of packing. */
        case PANEL_CONV_METHOD:     n = snprintf(buffer, size, "%s", v == 1 ? (g_conv_is_7z ? T("Copy (stored)") : T("Store"))
                                                                              : (g_conv_is_7z ? T("LZMA") : T("Deflate"))); break;
        default: break;
    }
    return (proven_u8str_view_t){ .ptr = (const proven_byte_t *)buffer, .size = n > 0 ? (size_t)n : 0 };
}

void print_now(app_state_t *app) {
    rubraview_print_options_t o = print_options(app);
    size_t count = app->print_list_count, printed = 0;
    if (count == 0) return;
    bool printed_ok = false;
    if (app->print_printer >= 0 && !app->print_with_dialog) {   /* 1.19.0: there, as chosen, no dialog */
        int32_t paper = app->print_paper >= 0 && (size_t)app->print_paper < app->print_paper_count ? app->print_papers[app->print_paper].id : 0;
        int32_t copies = (int32_t)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("print"), PROVEN_LIT("copies")));
        printed_ok = rubraview_pal_print_pictures_on(print_printer_name(app), paper, print_orientation(app), copies, app->print_list, count, &o, &printed);
    } else {
        printed_ok = rubraview_pal_print_pictures(rubraview_pal_window_native_handle(app->window), app->print_list, count, &o, &printed);
    }
    app->print_with_dialog = false;
    if (!printed_ok) {
        osd_say(app, TU8("not printed"));
        return;
    }
    char line[96];
    if (printed == count) snprintf(line, sizeof(line), T("%zu sent to the printer"), printed);
    else snprintf(line, sizeof(line), T("%zu sent to the printer, %zu could not be read"), printed, count - printed);
    osd_say(app, cstr(line));
}

bool arc_temp_path(char *dst, size_t cap, const char *tag, proven_u8str_view_t leaf);

void print_temp_drop(app_state_t *app) {
    if (!app->print_temp[0]) return;
    (void)rubraview_pal_fs_delete(cstr(app->print_temp));
    app->print_temp[0] = '\0';
}

/* D-89: a page of an archive has no file to hand the printer's reader, so
   it is laid in the temporary folder for as long as the print takes. */
void print_archive_page(app_state_t *app) {
    int32_t page = current_page_index(app);
    proven_u8str_view_t shown = rubraview_path_basename(page_display_name(app, (size_t)page));
    proven_u8str_view_t stem = rubraview_path_stem(shown);
    proven_u8str_view_t ext = { .ptr = shown.ptr + stem.size, .size = shown.size - stem.size };
    print_temp_drop(app);
    bool ok = arc_temp_path(app->print_temp, sizeof(app->print_temp), "print", ext);
    if (ok) {
        page_job_stop(app);   /* the archive is the main thread's to read */
        size_t budget = rubraview_page_source_read_budget(&app->source, (size_t)page);
        void *mem = budget > 0 ? malloc(budget) : NULL;
        ok = false;
        if (mem) {
            proven_arena_t arena = proven_arena_create((proven_mem_mut_t){ .ptr = (proven_byte_t*)mem, .size = budget });
            rubraview_page_bytes_t bytes = rubraview_page_source_read(&arena, &app->source, (size_t)page, MAX_PAGE_BYTES);
            ok = bytes.ok && bytes.data.size > 0 && rubraview_pal_fs_write_file(cstr(app->print_temp), bytes.data);
            free(mem);
        }
    }
    if (!ok) {
        app->print_temp[0] = '\0';
        osd_say(app, TU8("not printed: the page could not be read"));
        return;
    }
    proven_u8str_view_t path = cstr(app->print_temp);
    char kept[sizeof(app->print_temp)];
    memcpy(kept, app->print_temp, sizeof(kept));
    print_paths(app, &path, 1);                       /* closes a panel that is open, which drops the file's name */
    memcpy(app->print_temp, kept, sizeof(kept));
}
