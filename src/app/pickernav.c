/*
 * The open-file screen's folders: the working arena kept from filling, the picker's own arenas, a folder listed
 * and sorted, back and forward, the focus, picks made a playlist, recycled or curated (RV-163, D-197: moved from main.c as it was).
 */
#include "app.h"

#define PICKER_ARENA_BYTES (32u * 1024u * 1024u)
#define APP_ARENA_LOW (16u * 1024u * 1024u)

/* The working arena is never given back, and when it filled, everything
   that took from it failed without a word (2026-09-29). What fills it
   fastest now has memory of its own (pages, the picker, subtitle
   pictures); as a last guard, a working arena running low goes on in a
   new block. The full one is kept: what is in it is still in use. */
void arena_refill(app_state_t *app) {
    /* RFC-0006: the persist region moves on to a new heap chunk; app->arena is that region's arena. */
    if (app->mem && !rubraview_region_ensure(&app->mem->persist, APP_ARENA_LOW)) app->mem->full = true;
}

/* The idle picker arena, emptied: the next listing is built in it. */
static proven_arena_t *picker_next_arena(app_state_t *app) {
    int side = app->picker_side ^ 1;
    if (!app->picker_arena_mem[side]) {
        app->picker_arena_mem[side] = malloc(PICKER_ARENA_BYTES);
        if (!app->picker_arena_mem[side]) return app->arena;   /* no room: the old way */
        app->picker_arenas[side] = proven_arena_create((proven_mem_mut_t){
            .ptr = (proven_byte_t*)app->picker_arena_mem[side], .size = PICKER_ARENA_BYTES });
    }
    proven_arena_reset(&app->picker_arenas[side]);
    return &app->picker_arenas[side];
}

/* A string kept past the listing it came from (an undo entry, a
   favourite, a path being opened), copied into the working arena. */
proven_u8str_view_t app_keep(app_state_t *app, proven_u8str_view_t text) {
    if (text.size == 0) return text;
    /* RFC-0006 (RV-141): room is made in the session's region; when there is none, nothing
       is kept and that is said — the text handed in is never given back as if it were kept,
       since a caller's stack buffer would then be held after it is gone. */
    if (app->mem && !rubraview_region_ensure(&app->mem->persist, text.size + 1)) { app->mem->full = true; return PROVEN_LIT(""); }
    proven_result_mem_mut_t res = proven_arena_alloc(app->arena, text.size + 1);
    if (!proven_is_ok(res.err)) { if (app->mem) app->mem->full = true; return PROVEN_LIT(""); }
    memcpy(res.value.ptr, text.ptr, text.size);
    res.value.ptr[text.size] = '\0';
    return (proven_u8str_view_t){ .ptr = (const proven_byte_t *)(const char*)res.value.ptr, .size = text.size };
}

/* The recent files as a listing (owner, 2026-10-16): newest first, each read from the
   disk so the details have its size and dates; one gone since is left out. */
static rubraview_fs_listing_t recent_listing(app_state_t *app, proven_arena_t *pa) {
    rubraview_fs_listing_t out = {0};
    recent_sync(app, PROVEN_LIT(""), false);   /* another viewer's too (D-104) */
    size_t newest[RECENT_KEPT];
    size_t shown = rubraview_recent_newest(&app->recent, newest, RECENT_KEPT);
    proven_result_mem_mut_t res = rubraview_arena_alloc_array(pa, shown ? shown : 1, sizeof(rubraview_fs_entry_t));
    if (!proven_is_ok(res.err)) return out;
    out.entries = (rubraview_fs_entry_t*)(void*)res.value.ptr;
    for (size_t r = 0; r < shown; ++r) {
        rubraview_fs_entry_t e;
        if (!rubraview_pal_fs_stat(pa, app->recent.list.entries[newest[r]].path, &e)) continue;
        out.entries[out.count++] = e;
    }
    return out;
}

/* The details view's order (owner, 2026-10-16): the column chosen, kept in
   Settings › Files; every array that follows the listing is put in the same order. */
static void picker_apply_sort(app_state_t *app) {
    int32_t choice = (int32_t)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("files"), PROVEN_LIT("picker_sort")));
    bool desc = rubraview_settings_get(&app->settings, PROVEN_LIT("files"), PROVEN_LIT("picker_sort_desc")) > 0.5;
    size_t n = app->picker_listing.count;
    if (choice <= 0 || n < 2 || rubraview_picker_is_places(app->picker_dir)) return;   /* as the folder has it */
    size_t *order = (size_t*)malloc(n * sizeof(size_t));
    rubraview_picker_media_t *media = (rubraview_picker_media_t*)calloc(n, sizeof(rubraview_picker_media_t));
    rubraview_fs_entry_t *entries = (rubraview_fs_entry_t*)malloc(n * sizeof(rubraview_fs_entry_t));
    struct picker_thumb *thumbs = (struct picker_thumb*)malloc(n * sizeof(struct picker_thumb));
    bool *picked = (bool*)malloc(n * sizeof(bool));
    if (order && media && entries && thumbs && picked) {
        bool have_thumbs = app->picker_thumbs && app->picker_thumb_count == n;
        for (size_t i = 0; i < n && have_thumbs; ++i) media[i] = app->picker_thumbs[i].media;
        /* RV-202: what the size reader read for this sort stands in where no thumbnail has said it yet */
        for (size_t i = 0; i < n; ++i) {
            const rubraview_fs_entry_t *e = &app->picker_listing.entries[i];
            if (e->is_directory) continue;
            double seconds = 0.0;
            if (media[i].seconds <= 0.0 && rubraview_shape_cache_length(&app->shape_cache, e->path, e->mtime, &seconds) && seconds > 0.0)
                media[i].seconds = seconds;
        }
        rubraview_picker_sort_order(&app->picker_listing, media, (rubraview_picker_sort_key_t)(choice - 1), desc, order);
        size_t focus = app->picker.focus;
        for (size_t i = 0; i < n; ++i) {
            entries[i] = app->picker_listing.entries[order[i]];
            if (have_thumbs) thumbs[i] = app->picker_thumbs[order[i]];
            picked[i] = app->picker_selected ? app->picker_selected[order[i]] : false;
            if (order[i] == focus) app->picker.focus = i;
        }
        memcpy((void*)app->picker_listing.entries, entries, n * sizeof(rubraview_fs_entry_t));
        if (have_thumbs) memcpy(app->picker_thumbs, thumbs, n * sizeof(struct picker_thumb));
        if (app->picker_selected) memcpy(app->picker_selected, picked, n * sizeof(bool));
        if (app->picker_in_book && app->picker_page_of) {
            /* a page's place in the book goes with its entry; `..` stays first */
            size_t skip = app->picker_page_skip;
            size_t *pages = (size_t*)malloc(n * sizeof(size_t));
            if (pages) {
                for (size_t i = skip; i < n; ++i) pages[i] = order[i] >= skip ? app->picker_page_of[order[i] - skip] : RUBRAVIEW_PICKER_NOT_A_PAGE;
                for (size_t i = skip; i < n; ++i) app->picker_page_of[i - skip] = pages[i];
                free(pages);
            }
        }
        app->picker.range_anchor = RUBRAVIEW_PICKER_NO_ANCHOR;
    }
    free(order); free(media); free(entries); free(thumbs); free(picked);
}

/* A column's name pressed: by it, rising; pressed again, falling. Kept in Settings › Files. */
bool picker_is_up(const app_state_t *app, size_t index);
void picker_filter_set(app_state_t *app, proven_u8str_view_t text);
void picker_filter_forget(app_state_t *app);
void ctx_close(app_state_t *app);
void picker_activate(app_state_t *app, size_t index);
void slidepop_close(app_state_t *app);
void listwin_sync(app_state_t *app);
void list_insert_paths(app_state_t *app, const proven_u8str_view_t *paths, size_t count, size_t to);
size_t picker_picked_entries(app_state_t *app, rubraview_fs_entry_t **out);

/* 1.24.0: by a column, rising or falling, chosen outright. */
void picker_sort_set(app_state_t *app, int key, bool desc) {
    /* RV-202: by length, every film's and song's length read first, not only the ones whose thumbnails came */
    if (key == RUBRAVIEW_PICKER_SORT_LENGTH && !app->picker_in_book &&
        facts_read_for_sort(app, SHAPE_FOR_PICKER, FACTS_THEN_PICKER_SORT, key, desc)) return;
    rubraview_settings_set(&app->settings, PROVEN_LIT("files"), PROVEN_LIT("picker_sort"), (double)(key + 1));
    rubraview_settings_set(&app->settings, PROVEN_LIT("files"), PROVEN_LIT("picker_sort_desc"), desc ? 1.0 : 0.0);
    settings_write_if_changed(app, false);
    char kept[sizeof(app->picker_filter)];
    memcpy(kept, app->picker_filter, sizeof(kept));
    if (kept[0]) picker_filter_set(app, PROVEN_LIT(""));   /* 1.22.0: the whole folder sorted, then filtered again */
    picker_apply_sort(app);
    if (kept[0]) picker_filter_set(app, cstr(kept));
    else app->picker_full = app->picker_listing;
    char line[64];
    int n = snprintf(line, sizeof(line), T("by %s, %s"), rubraview_picker_sort_name((rubraview_picker_sort_key_t)key),
                     desc ? "falling" : "rising");
    if (n > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n });
}

void picker_sort_by(app_state_t *app, int key) {
    int now = (int)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("files"), PROVEN_LIT("picker_sort"))) - 1;
    bool desc = rubraview_settings_get(&app->settings, PROVEN_LIT("files"), PROVEN_LIT("picker_sort_desc")) > 0.5;
    picker_sort_set(app, key, now == key ? !desc : false);
}

/* 1.24.0 (owner, 2026-10-19): the folders the open-file screen went through, for back and forward. */
#define PICKER_HIST 24
#define PICKER_HIST_PATH 1024

static void picker_hist_push(app_state_t *app, bool forward, proven_u8str_view_t dir) {
    if (dir.size == 0 || dir.size >= PICKER_HIST_PATH) return;
    if (!app->picker_hist) app->picker_hist = calloc(2 * PICKER_HIST, PICKER_HIST_PATH);
    if (!app->picker_hist) return;
    char (*stack)[PICKER_HIST_PATH] = app->picker_hist + (forward ? PICKER_HIST : 0);
    int32_t *n = forward ? &app->picker_fwd_n : &app->picker_back_n;
    if (*n == PICKER_HIST) { memmove(stack[0], stack[1], (PICKER_HIST - 1) * PICKER_HIST_PATH); --*n; }   /* the oldest goes */
    memcpy(stack[*n], dir.ptr, dir.size);
    stack[*n][dir.size] = '\0';
    ++*n;
}

static void picker_navigate_to(app_state_t *app, proven_u8str_view_t dir, bool from_history);

void picker_navigate(app_state_t *app, proven_u8str_view_t dir) {
    picker_navigate_to(app, dir, false);
}

/* Back (forward false) or forward through the folders gone through; Alt+Left, Alt+Right. */
void picker_history_go(app_state_t *app, bool forward) {
    int32_t *from = forward ? &app->picker_fwd_n : &app->picker_back_n;
    if (*from <= 0 || !app->picker_hist) { osd_say(app, forward ? TU8("nothing to go forward to") : TU8("nothing to go back to")); return; }
    char target[PICKER_HIST_PATH];
    snprintf(target, sizeof(target), "%s", app->picker_hist[(forward ? PICKER_HIST : 0) + --*from]);
    picker_hist_push(app, !forward, app->picker_dir);
    picker_navigate_to(app, cstr(target), true);
    app->picker.focus = 0;
}

void picker_go_up(app_state_t *app) {
    proven_u8str_view_t parent = rubraview_picker_parent(app->picker_dir);
    if (parent.size == 0) return;
    picker_navigate(app, parent);
    app->picker.focus = 0;
}

static void picker_navigate_to(app_state_t *app, proven_u8str_view_t dir, bool from_history) {
    /* Where it was, kept before the listing's memory turns over; another folder goes on the way back. */
    char was[PICKER_HIST_PATH];
    size_t was_len = app->picker_dir.size < sizeof(was) ? app->picker_dir.size : 0;
    if (was_len) memcpy(was, app->picker_dir.ptr, was_len);
    if (!from_history && app->picker_open && was_len && !rubraview_path_same((proven_u8str_view_t){ .ptr = (const proven_byte_t *)was, .size = was_len }, dir)) {
        picker_hist_push(app, false, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)was, .size = was_len });
        app->picker_fwd_n = 0;
    }
    proven_arena_t *pa = picker_next_arena(app);
    /* The PC page lists drives and the usual folders, in the PAL's order
       (owner, 2026-09-28: the whole machine, not only the folder the
       viewer was started in). */
    bool places = rubraview_picker_is_places(dir);
    bool recent = rubraview_picker_is_recent(dir);
    /* Inside the book being read (owner, 2026-09-28): its own folders and
       pages, a level at a time, in the order they are read. */
    proven_u8str_view_t inner = PROVEN_LIT("");
    bool in_book = !places && !recent && app->source.kind != RUBRAVIEW_PAGE_SOURCE_FOLDER &&
                   rubraview_picker_inside_archive(dir, app->source.archive_path, &inner);
    rubraview_archive_level_t level = {0};
    if (in_book) {
        level = rubraview_picker_archive_level(pa, app->source.archive_path,
                                               app->source.pages, app->source.page_count, inner);
    }
    /* "." has no parent by name, which kept the reader below the folder the
       viewer was run from (owner, 2026-09-28): every folder is made whole. */
    if (!places && !in_book && !recent) dir = rubraview_pal_fs_absolute(pa, dir);
    rubraview_fs_listing_t listing = in_book ? level.listing
                                   : places ? rubraview_pal_fs_list_places(pa)
                                   : recent ? recent_listing(app, pa)
                                            : rubraview_pal_fs_list_dir(pa, dir);
    /* The reader's favourites first on the PC page, every one of them (owner,
       2026-10-17: the places bar shows as many as fit; the list shows all). */
    if (places && app->favorites.count > 0) {
        proven_result_mem_mut_t res = rubraview_arena_alloc_array(pa, app->favorites.count + listing.count, sizeof(rubraview_fs_entry_t));
        if (proven_is_ok(res.err)) {
            rubraview_fs_entry_t *all = (rubraview_fs_entry_t *)(void *)res.value.ptr;
            for (size_t i = 0; i < app->favorites.count; ++i) {
                rubraview_fs_entry_t st;   /* 1.24.0: a favourite may be a file */
                bool folder = !rubraview_pal_fs_stat(pa, app->favorites.paths[i], &st) || st.is_directory;
                all[i] = (rubraview_fs_entry_t){ .name = rubraview_favorites_title(&app->favorites, i),
                                                 .path = app->favorites.paths[i], .is_directory = folder };
            }
            if (listing.count) memcpy(all + app->favorites.count, listing.entries, listing.count * sizeof(rubraview_fs_entry_t));
            listing.entries = all;
            listing.count += app->favorites.count;
        }
    }
    /* Only folders and what the viewer opens (owner, 2026-09-21). */
    char kinds_buf[1024];
    size_t hidden = in_book ? 0 : rubraview_picker_keep_openable(&listing, kinds_with_text(kinds_buf, sizeof(kinds_buf), IMAGE_FILTER ";" MEDIA_FILTER ";" ARCHIVE_FILTER ";*.cue;" LIST_FILTER));
    /* A RAR set is one book (owner, 2026-10-01): its first volume stands for it. */
    if (!in_book) {
        size_t kept = 0;
        for (size_t i = 0; i < listing.count; ++i) {
            if (!listing.entries[i].is_directory && rubraview_page_source_is_later_volume(listing.entries[i].name)) { hidden++; continue; }
            listing.entries[kept++] = listing.entries[i];
        }
        listing.count = kept;
    }
    if (listing.count == 0 && rubraview_picker_parent(dir).size == 0) {
        if (hidden > 0) osd_say(app, TU8("nothing in that folder can be opened here"));
        return;
    }
    if (listing.count == 0 && hidden > 0) {
        /* D-18 kept the reader out of a folder with nothing to open; with
           `..` on screen it is no longer a trap, so it is only said. */
        osd_say(app, TU8("nothing here can be opened"));
    }
    app->picker_hidden = hidden;

    /* Directories first, then files, each in the Sort by order — folders are
       what a reader scans for first on a touch screen. */
    rubraview_sort_item_t *items = NULL;
    proven_result_mem_mut_t res = rubraview_arena_alloc_array(pa, listing.count, sizeof(rubraview_sort_item_t));
    if (!places && !in_book && !recent && proven_is_ok(res.err)) {   /* the recent files stay newest first */
        items = (rubraview_sort_item_t*)(void*)res.value.ptr;
        for (size_t i = 0; i < listing.count; ++i) {
            items[i] = (rubraview_sort_item_t){
                .name = listing.entries[i].name,
                .mtime = listing.entries[i].mtime,
                .ctime = listing.entries[i].ctime,
                .size_bytes = listing.entries[i].size_bytes,
                .tag = (uint64_t)i,
            };
        }
        /* Settings > Files > Sort by, as the pages are turned (owner,
           2026-09-28); the two passes below put the folders first. */
        bool ascending = true;
        rubraview_sort_mode_t mode = folder_sort_mode(app, &ascending);
        rubraview_sort_items(items, listing.count, mode, ascending, NULL);

        proven_result_mem_mut_t ordered_res =
            rubraview_arena_alloc_array(pa, listing.count, sizeof(rubraview_fs_entry_t));
        if (proven_is_ok(ordered_res.err)) {
            rubraview_fs_entry_t *ordered = (rubraview_fs_entry_t*)(void*)ordered_res.value.ptr;
            size_t out = 0;
            for (int folder_pass = 1; folder_pass >= 0; --folder_pass) {
                for (size_t i = 0; i < listing.count; ++i) {
                    const rubraview_fs_entry_t *e = &listing.entries[items[i].tag];
                    if ((int)e->is_directory != folder_pass) continue;
                    ordered[out++] = *e;
                }
            }
            listing.entries = ordered;
        }
    }

    /* `..` first, wherever there is a folder above this one (owner,
       2026-09-23). `Ctrl+Backspace` did this already, but not by touch.
       Above a drive is the PC page, so every folder has one. */
    proven_u8str_view_t parent = rubraview_picker_parent(dir);
    app->picker_in_book = in_book;
    app->picker_page_of = level.page_of;
    app->picker_page_skip = 0;
    if (parent.size > 0) {
        proven_result_mem_mut_t up_res =
            rubraview_arena_alloc_array(pa, (listing.count + 1), sizeof(rubraview_fs_entry_t));
        if (proven_is_ok(up_res.err)) {
            rubraview_fs_entry_t *with_up = (rubraview_fs_entry_t*)(void*)up_res.value.ptr;
            with_up[0] = (rubraview_fs_entry_t){
                .name = PROVEN_LIT(".."),
                .path = parent,
                .is_directory = true,
            };
            memcpy(with_up + 1, listing.entries, listing.count * sizeof(rubraview_fs_entry_t));
            listing.entries = with_up;
            listing.count += 1;
            app->picker_page_skip = 1;
        }
    }

    int32_t win_w = 0, win_h = 0;
    rubraview_pal_window_get_size(app->window, &win_w, &win_h);
    double dpi = rubraview_pal_window_dpi_scale(app->window);
    double tile = PICKER_TILE * dpi;

    proven_result_mem_mut_t sel_res = rubraview_arena_alloc_array(pa, listing.count, sizeof(bool));
    app->picker_selected = proven_is_ok(sel_res.err) ? (bool*)(void*)sel_res.value.ptr : NULL;
    if (app->picker_selected) memset(app->picker_selected, 0, listing.count * sizeof(bool));

    rubraview_picker_mode_t mode = app->picker.mode;   /* the mode outlives the folder */
    picker_thumbs_free(app);
    app->picker_generation++;
    if (app->thumbs) rubraview_pal_thumbs_generation(app->thumbs, app->picker_generation);
    app->picker_thumbs = (struct picker_thumb *)calloc(listing.count ? listing.count : 1, sizeof(*app->picker_thumbs));
    app->picker_thumb_count = app->picker_thumbs ? listing.count : 0;
    app->picker_dir = dir;
    if (pa != app->arena) app->picker_side ^= 1;   /* the new listing is the one shown */
    app->picker_listing = listing;
    app->picker = rubraview_picker_create(&app->picker_listing, tile,
                                          (double)win_h - picker_top(app) - (PICKER_HEADER_HEIGHT + PICKER_ACTION_HEIGHT) * dpi,
                                          picker_columns(app));
    picker_layout(app);
    app->picker.selected = app->picker_selected;
    rubraview_picker_set_mode(&app->picker, mode);
    /* The focus starts on the first file: `..` and the folders are for
       going somewhere, and "Same type" reads the focused file. */
    for (size_t i = 0; i < listing.count; ++i) {
        if (!listing.entries[i].is_directory) { app->picker.focus = i; break; }
    }
    picker_apply_sort(app);   /* the column chosen in the details view, kept (owner, 2026-10-16) */
    picker_filter_forget(app);   /* 1.22.0: the folder is what a filter is taken from */
    /* RV-202: kept by length: the lengths not known yet are read, and the folder sorted again when they are */
    int32_t kept = (int32_t)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("files"), PROVEN_LIT("picker_sort"))) - 1;
    if (kept == RUBRAVIEW_PICKER_SORT_LENGTH && !app->picker_in_book && !rubraview_picker_is_places(app->picker_dir))
        (void)facts_read_for_sort(app, SHAPE_FOR_PICKER, FACTS_THEN_PICKER_SORT, kept,
                                  rubraview_settings_get(&app->settings, PROVEN_LIT("files"), PROVEN_LIT("picker_sort_desc")) > 0.5);
}

/* The extension of the item the focus is on, ".jpg" and such. */
proven_u8str_view_t picker_focus_extension(const app_state_t *app) {
    proven_u8str_view_t none = PROVEN_LIT("");
    if (app->picker.focus < app->picker_listing.count) {
        const rubraview_fs_entry_t *entry = &app->picker_listing.entries[app->picker.focus];
        if (!entry->is_directory) return rubraview_path_ext(entry->name);
    }
    /* The focus is on a folder: the first picked file answers instead. */
    for (size_t i = 0; i < app->picker_listing.count; ++i) {
        if (!app->picker_selected || !app->picker_selected[i]) continue;
        if (app->picker_listing.entries[i].is_directory) continue;
        return rubraview_path_ext(app->picker_listing.entries[i].name);
    }
    return none;
}

void rename_extension_begin(app_state_t *app);
void rename_picker_begin(app_state_t *app, size_t at);
void ext_switch(app_state_t *app, int32_t row);
void rename_extension_apply(app_state_t *app);
void text_find_step(app_state_t *app, bool forward);
void rename_picker_commit(app_state_t *app);
void open_set_from_entries(app_state_t *app, rubraview_fs_entry_t *entries, size_t count, proven_u8str_view_t said);

/* The picked files, gathered as a listing the page source can take. */

size_t picker_picked_entries(app_state_t *app, rubraview_fs_entry_t **out) {
    size_t picked = 0;
    for (size_t i = 0; i < app->picker_listing.count; ++i) {
        if (app->picker_selected && app->picker_selected[i] && !picker_is_up(app, i)) picked++;   /* 1.22.0: folders too */
    }
    if (picked == 0) return 0;
    proven_result_mem_mut_t res = rubraview_arena_alloc_array(app->arena, picked, sizeof(rubraview_fs_entry_t));
    if (!proven_is_ok(res.err)) return 0;
    rubraview_fs_entry_t *entries = (rubraview_fs_entry_t*)(void*)res.value.ptr;
    size_t n = 0;
    for (size_t i = 0; i < app->picker_listing.count; ++i) {
        if (!app->picker_selected || !app->picker_selected[i]) continue;
        if (picker_is_up(app, i)) continue;
        entries[n++] = app->picker_listing.entries[i];
    }
    *out = entries;
    return n;
}

/* The picked files as a playlist of their own, then opened (owner,
   2026-09-23, who preferred this to opening them loose). The file is
   `PlaylistNNNN.m3u8` in this folder — the first number not taken. */
void picker_make_playlist(app_state_t *app) {
    rubraview_fs_entry_t *entries = NULL;
    size_t n = picker_picked_entries(app, &entries);
    if (n == 0) { osd_say(app, TU8("pick some files first")); return; }

    rubraview_playlist_t list = {0};
    proven_result_mem_mut_t res = rubraview_arena_alloc_array(app->arena, n, sizeof(rubraview_playlist_entry_t));
    if (!proven_is_ok(res.err)) return;
    list.entries = (rubraview_playlist_entry_t*)(void*)res.value.ptr;
    size_t files = 0;
    for (size_t i = 0; i < n; ++i) {
        if (entries[i].is_directory) continue;   /* 1.22.0: a folder picked is not a song */
        list.entries[files] = (rubraview_playlist_entry_t){ .path = entries[i].name };   /* beside the file */
        entries[files++] = entries[i];
    }
    if (files == 0) { osd_say(app, TU8("pick some files first")); return; }
    n = files;
    list.count = n;

    proven_u8str_view_t text = rubraview_playlist_serialize_m3u8(app->arena, &list);
    if (text.size == 0) { osd_say(app, TU8("could not write the playlist")); return; }

    char name[32];
    proven_u8str_view_t target = PROVEN_LIT("");
    for (int number = 1; number <= 9999; ++number) {
        int written = snprintf(name, sizeof(name), "Playlist%04d.m3u8", number);
        if (written <= 0) return;
        proven_u8str_view_t candidate = rubraview_path_join(app->arena, app->picker_dir,
                                                (proven_u8str_view_t){ .ptr = (const proven_byte_t *)name, .size = (size_t)written });
        rubraview_fs_entry_t taken;
        if (!rubraview_pal_fs_stat(app->arena, candidate, &taken)) { target = candidate; break; }
    }
    if (target.size == 0) { osd_say(app, TU8("too many playlists in this folder")); return; }
    if (!rubraview_pal_fs_write_file(target, text)) { osd_say(app, TU8("could not write the playlist")); return; }

    char line[96];
    int written = snprintf(line, sizeof(line), T("%zu file(s) in %s"), n, name);
    open_set_from_entries(app, entries, n, written > 0 ? (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)written }
                                                       : TU8("playlist opened"));
}

/* The picked files to the recycle bin, once the button has been pressed
   twice (§3.18.1 asks before anything leaves the disk). */
void picker_recycle(app_state_t *app) {
    rubraview_fs_entry_t *entries = NULL;
    size_t n = picker_picked_entries(app, &entries);
    if (n == 0) { osd_say(app, TU8("pick some files first")); return; }

    double now = rubraview_pal_time_now_seconds();
    if (!rubraview_confirm_armed(&app->picker_confirm, PICKER_BTN_RECYCLE, now)) {
        rubraview_confirm_press(&app->picker_confirm, PICKER_BTN_RECYCLE, now);
        char line[96];
        int written = snprintf(line, sizeof(line), T("press Recycle again to bin %zu file(s)"), n);
        if (written > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)written });
        return;
    }
    rubraview_confirm_clear(&app->picker_confirm);

    size_t done = 0;
    uint32_t batch = ++app->undo_batch;
    for (size_t i = 0; i < n; ++i) {
        if (!rubraview_pal_fs_recycle(entries[i].path)) continue;
        rubraview_undo_push(&app->undo, (rubraview_file_action_t){
            .op = RUBRAVIEW_FILE_OP_RECYCLE,
            .source_path = app_keep(app, entries[i].path),
            .target_path = app_keep(app, entries[i].path),
            .batch = batch,
        });
        done++;
    }
    char line[96];
    int written = snprintf(line, sizeof(line), T("%zu file(s) in the recycle bin"), done);
    if (written > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)written });
    picker_navigate(app, app->picker_dir);
}

/* Move or copy the picked files into a folder. */
void picker_curate_to(app_state_t *app, proven_u8str_view_t target_dir, bool moving) {
    rubraview_fs_entry_t *entries = NULL;
    size_t n = picker_picked_entries(app, &entries);
    if (n == 0) { osd_say(app, TU8("pick some files first")); return; }

    rubraview_pal_fs_make_dirs(target_dir);
    size_t done = 0;
    uint32_t batch = ++app->undo_batch;   /* 1.23.0: one undo for all of them */
    for (size_t i = 0; i < n; ++i) {
        proven_u8str_view_t target = rubraview_path_join(app->arena, target_dir, entries[i].name);
        if (target.size == 0) continue;
        bool ok = moving ? rubraview_pal_fs_move(entries[i].path, target)
                         : entries[i].is_directory ? rubraview_pal_fs_copy_tree(entries[i].path, target)   /* 1.22.0 */
                                                   : rubraview_pal_fs_copy(entries[i].path, target);
        if (!ok) continue;
        rubraview_undo_push(&app->undo, (rubraview_file_action_t){
            .op = moving ? RUBRAVIEW_FILE_OP_MOVE : RUBRAVIEW_FILE_OP_COPY,
            .source_path = app_keep(app, entries[i].path),
            .target_path = target,
            .batch = batch,
        });
        done++;
    }
    char line[128];
    int written = snprintf(line, sizeof(line), T("%zu file(s) %s %.*s"), done, moving ? T("moved to") : T("copied to"),
                           (int)target_dir.size, target_dir.ptr);
    if (written > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)written });
    if (moving) picker_navigate(app, app->picker_dir);
}

/* Move or copy the picked files into the curation folder a digit names. */
void picker_curate(app_state_t *app, int32_t digit, bool moving) {
    proven_u8str_view_t target_dir = rubraview_curation_target(&app->curation, digit);
    if (target_dir.size == 0) { osd_say(app, TU8("no folder is bound to that number")); return; }
    picker_curate_to(app, target_dir, moving);
}

bool curation_has_numbers(const app_state_t *app) {
    for (int32_t d = 1; d <= 9; ++d) if (rubraview_curation_target(&app->curation, d).size > 0) return true;
    return false;
}
