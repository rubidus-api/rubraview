/*
 * The open-file screen's folders: the working arena kept from filling, the picker's own arenas, a folder listed
 * and sorted, back and forward, the focus, picks made a playlist, recycled or curated (RV-163, D-197: moved from main.c as it was).
 */
#include "app.h"
#include "rubraview/archive_job.h"

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
        app->picker_arena_mem[side] = rubraview_alloc(PICKER_ARENA_BYTES);
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
    proven_sys_mem_copy(res.value.ptr, text.ptr, text.size);
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
    size_t *order = (size_t*)rubraview_alloc(n * sizeof(size_t));
    rubraview_picker_media_t *media = (rubraview_picker_media_t*)rubraview_alloc_zero(n, sizeof(rubraview_picker_media_t));
    rubraview_fs_entry_t *entries = (rubraview_fs_entry_t*)rubraview_alloc(n * sizeof(rubraview_fs_entry_t));
    struct picker_thumb *thumbs = (struct picker_thumb*)rubraview_alloc(n * sizeof(struct picker_thumb));
    bool *picked = (bool*)rubraview_alloc(n * sizeof(bool));
    if (order && media && entries && thumbs && picked) {
        bool have_thumbs = app->picker_thumbs && app->picker_thumb_count == n;
        for (size_t i = 0; i < n && have_thumbs; ++i) media[i] = app->picker_thumbs[i].media;
        /* RV-202, RV-210: what the reader read stands in where no thumbnail has said it yet */
        for (size_t i = 0; i < n; ++i) {
            const rubraview_fs_entry_t *e = &app->picker_listing.entries[i];
            if (e->is_directory) continue;
            int32_t w = 0, h = 0;
            double seconds = 0.0;
            if (!rubraview_shape_cache_facts(&app->shape_cache, e->path, e->mtime, &w, &h, &seconds)) continue;
            if (media[i].seconds <= 0.0 && seconds > 0.0) media[i].seconds = seconds;
            if ((media[i].width <= 0 || media[i].height <= 0) && w > 0 && h > 0) { media[i].width = w; media[i].height = h; }
        }
        rubraview_picker_sort_order(&app->picker_listing, media, (rubraview_picker_sort_key_t)(choice - 1), desc, order);
        size_t focus = app->picker.focus;
        for (size_t i = 0; i < n; ++i) {
            entries[i] = app->picker_listing.entries[order[i]];
            if (have_thumbs) thumbs[i] = app->picker_thumbs[order[i]];
            picked[i] = app->picker_selected ? app->picker_selected[order[i]] : false;
            if (order[i] == focus) app->picker.focus = i;
        }
        proven_sys_mem_copy((void*)app->picker_listing.entries, entries, n * sizeof(rubraview_fs_entry_t));
        if (have_thumbs) proven_sys_mem_copy(app->picker_thumbs, thumbs, n * sizeof(struct picker_thumb));
        if (app->picker_selected) proven_sys_mem_copy(app->picker_selected, picked, n * sizeof(bool));
        if (app->picker_in_book && app->picker_page_of) {
            /* a page's place in the book goes with its entry; `..` stays first */
            size_t skip = app->picker_page_skip;
            size_t *pages = (size_t*)rubraview_alloc(n * sizeof(size_t));
            if (pages) {
                for (size_t i = skip; i < n; ++i) pages[i] = order[i] >= skip ? app->picker_page_of[order[i] - skip] : RUBRAVIEW_PICKER_NOT_A_PAGE;
                for (size_t i = skip; i < n; ++i) app->picker_page_of[i - skip] = pages[i];
                rubraview_free(pages);
            }
        }
        app->picker.range_anchor = RUBRAVIEW_PICKER_NO_ANCHOR;
    }
    rubraview_free(order); rubraview_free(media); rubraview_free(entries); rubraview_free(thumbs); rubraview_free(picked);
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

/* The order kept, made again over the whole folder (and filtered again): RV-210, after the facts were read ahead. */
void picker_resort(app_state_t *app) {
    char kept[sizeof(app->picker_filter)];
    proven_sys_mem_copy(kept, app->picker_filter, sizeof(kept));
    if (kept[0]) picker_filter_set(app, PROVEN_LIT(""));   /* 1.22.0: the whole folder sorted, then filtered again */
    picker_apply_sort(app);
    if (kept[0]) picker_filter_set(app, cstr(kept));
    else app->picker_full = app->picker_listing;
}

/* 1.24.0: by a column, rising or falling, chosen outright. */
void picker_sort_set(app_state_t *app, int key, bool desc) {
    /* RV-202: by length, every film's and song's length read first, not only the ones whose thumbnails came;
       RV-210: by resolution, every picture's and film's size likewise */
    if ((key == RUBRAVIEW_PICKER_SORT_LENGTH || key == RUBRAVIEW_PICKER_SORT_RESOLUTION) && !app->picker_in_book &&
        facts_read_for_sort(app, SHAPE_FOR_PICKER, FACTS_THEN_PICKER_SORT, key, desc)) return;
    rubraview_settings_set(&app->settings, PROVEN_LIT("files"), PROVEN_LIT("picker_sort"), (double)(key + 1));
    rubraview_settings_set(&app->settings, PROVEN_LIT("files"), PROVEN_LIT("picker_sort_desc"), desc ? 1.0 : 0.0);
    settings_write_if_changed(app, false);
    picker_resort(app);
    char line[64];
    int n = rubraview_fmt(line, sizeof(line), T("by {}, {}"), PROVEN_ARG(rubraview_picker_sort_name((rubraview_picker_sort_key_t)key)),
                     PROVEN_ARG(desc ? "falling" : "rising"));
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
    if (!app->picker_hist) app->picker_hist = rubraview_alloc_zero(2 * PICKER_HIST, PICKER_HIST_PATH);
    if (!app->picker_hist) return;
    char (*stack)[PICKER_HIST_PATH] = app->picker_hist + (forward ? PICKER_HIST : 0);
    int32_t *n = forward ? &app->picker_fwd_n : &app->picker_back_n;
    if (*n == PICKER_HIST) { proven_sys_mem_move(stack[0], stack[1], (PICKER_HIST - 1) * PICKER_HIST_PATH); --*n; }   /* the oldest goes */
    proven_sys_mem_copy(stack[*n], dir.ptr, dir.size);
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
    rubraview_fmt(target, sizeof(target), "{}", PROVEN_ARG(app->picker_hist[(forward ? PICKER_HIST : 0) + --*from]));
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

/* ---- RV-207: an archive's whole tree, a level at a time ---- */

bool picker_in_archive(const app_state_t *app) {
    return app->picker_arc_path && app->picker_dir.size > 0 &&
           rubraview_picker_inside_archive(app->picker_dir, proven_u8str_view_from_cstr(app->picker_arc_path), NULL);
}

void picker_leave_archive(app_state_t *app) {
    if (app->picker_arc_index) { rubraview_archive_index_free(app->picker_arc_index); rubraview_free(app->picker_arc_index); }
    rubraview_free(app->picker_arc_path);
    app->picker_arc_index = NULL;
    app->picker_arc_path = NULL;
}

/* A path listed inside the archive as the archive's own name for it ("a/b.jpg"); false for one outside it. */
bool picker_archive_rel(const app_state_t *app, proven_u8str_view_t path, char *out, size_t cap) {
    proven_u8str_view_t rel;
    if (!app->picker_arc_path || !rubraview_picker_inside_archive(path, proven_u8str_view_from_cstr(app->picker_arc_path), &rel) ||
        rel.size >= cap) return false;
    for (size_t i = 0; i < rel.size; ++i) out[i] = rel.ptr[i] == '\\' ? '/' : (char)rel.ptr[i];
    out[rel.size] = '\0';
    return true;
}

static proven_u8str_view_t arena_text(proven_arena_t *pa, const char *a, size_t an, const char *b, size_t bn) {
    proven_result_mem_mut_t res = proven_arena_alloc(pa, an + bn + 1);
    if (!proven_is_ok(res.err)) return PROVEN_LIT("");
    char *s = (char *)(void *)res.value.ptr;
    if (an) proven_sys_mem_copy(s, a, an);
    if (bn) proven_sys_mem_copy(s + an, b, bn);
    s[an + bn] = '\0';
    return (proven_u8str_view_t){ .ptr = (const proven_byte_t *)s, .size = an + bn };
}

static rubraview_fs_listing_t archive_listing(app_state_t *app, proven_arena_t *pa, proven_u8str_view_t dir) {
    rubraview_fs_listing_t listing = { 0 };
    const rubraview_archive_index_t *ix = app->picker_arc_index;
    char inner[RUBRAVIEW_ARCHIVE_PATH];
    if (!ix || ix->count == 0 || !picker_archive_rel(app, dir, inner, sizeof inner)) return listing;
    rubraview_archive_child_t *kids = rubraview_alloc(ix->count * sizeof *kids);
    if (!kids) return listing;
    size_t n = rubraview_archive_index_level(ix, proven_u8str_view_from_cstr(inner), kids, ix->count);
    proven_result_mem_mut_t res = rubraview_arena_alloc_array(pa, n ? n : 1, sizeof(rubraview_fs_entry_t));
    if (proven_is_ok(res.err)) {
        rubraview_fs_entry_t *e = (rubraview_fs_entry_t *)(void *)res.value.ptr;
        size_t dn = dir.size;
        while (dn > 0 && rubraview_path_is_sep(dir.ptr[dn - 1])) dn--;
        proven_u8str_view_t base = arena_text(pa, (const char *)dir.ptr, dn, "\\", 1);
        for (size_t i = 0; i < n; ++i) {
            proven_u8str_view_t path = arena_text(pa, (const char *)base.ptr, base.size, (const char *)kids[i].name.ptr, kids[i].name.size);
            e[i] = (rubraview_fs_entry_t){ .name = { .ptr = path.ptr + base.size, .size = kids[i].name.size }, .path = path,
                                           .size_bytes = kids[i].size, .mtime = kids[i].mtime, .ctime = kids[i].mtime,
                                           .is_directory = kids[i].is_dir };
        }
        listing.entries = e;
        listing.count = n;
    }
    rubraview_free(kids);
    return listing;
}

/* What the viewer itself opens: pictures, films, music, text, books, cue sheets and lists (D-258: the rest its program). */
bool picker_viewer_opens(proven_u8str_view_t name) {
    char kinds_buf[1024];
    return rubraview_glob_match_list(name, kinds_with_text(kinds_buf, sizeof(kinds_buf), IMAGE_FILTER ";" MEDIA_FILTER ";" ARCHIVE_FILTER ";*.cue;" LIST_FILTER));
}

/* D-258 (owner, 2026-11-02: "연결파일만 보여주는 것과 전체 파일 다 보여주는걸 상단 버튼에서 선택"): Settings › Files. */
static void picker_navigate_to(app_state_t *app, proven_u8str_view_t dir, bool from_history);

bool picker_every_file(const app_state_t *app) {
    return rubraview_settings_get(&app->settings, PROVEN_LIT("files"), PROVEN_LIT("picker_all")) > 0.5;
}

/* The tool row's Every file / Viewer's files: the other, kept, and the folder listed again. */
void picker_toggle_every_file(app_state_t *app) {
    bool all = !picker_every_file(app);
    rubraview_settings_set(&app->settings, PROVEN_LIT("files"), PROVEN_LIT("picker_all"), all ? 1.0 : 0.0);
    proven_u8str_view_t here = app_keep(app, app->picker_dir);
    picker_navigate_to(app, here, true);
    osd_say(app, all ? TU8("every file shown") : TU8("only what the viewer opens shown"));
}

/* An archive's names, its passwords tried when they are locked: none, then the session's, newest first. */
int picker_index_read(app_state_t *app, const char *path, rubraview_codepage_t codepage, struct rubraview_archive_index *ix) {
    rubraview_archive_result_t r = rubraview_archive_index_read(path, codepage, NULL, ix);
    for (size_t k = app->password_count; r == RUBRAVIEW_ARCHIVE_PASSWORD_NEEDED && k > 0; --k) {
        char pw[513];
        rubraview_fmt(pw, sizeof pw, "{}", PROVEN_ARG(rubraview_bytes_view(app->passwords[k - 1], (int)app->password_lens[k - 1])));
        r = rubraview_archive_index_read(path, codepage, pw, ix);
        secure_wipe(rubraview_mem_mut(pw, sizeof pw));
    }
    return r;
}

/* D-258: in the book being read, what is in its archive and is no page (a text, a ComicInfo.xml, a folder of
   them) after the folders and pages, as no page; its names unread (locked), the pages alone. */
static void book_listing_whole(app_state_t *app, proven_arena_t *pa, rubraview_archive_level_t *level,
                               proven_u8str_view_t inner, proven_u8str_view_t dir) {
    proven_u8str_view_t archive = rubraview_page_source_first_volume(&app->source);
    char path[RUBRAVIEW_ARCHIVE_PATH], rel[RUBRAVIEW_ARCHIVE_PATH];
    if (archive.size == 0 || archive.size >= sizeof path || inner.size >= sizeof rel) return;
    proven_sys_mem_copy(path, archive.ptr, archive.size);
    path[archive.size] = '\0';
    for (size_t i = 0; i < inner.size; ++i) rel[i] = inner.ptr[i] == '\\' ? '/' : (char)inner.ptr[i];
    rel[inner.size] = '\0';
    rubraview_archive_index_t *ix = rubraview_alloc_zero(1, sizeof *ix);
    if (!ix) return;
    rubraview_codepage_t cp = app->names_codepage >= 0 ? (rubraview_codepage_t)app->names_codepage : app->archive_codepage;
    if (picker_index_read(app, path, cp, ix) != RUBRAVIEW_ARCHIVE_DONE) { rubraview_free(ix); return; }
    rubraview_archive_child_t *kids = ix->count ? rubraview_alloc(ix->count * sizeof *kids) : NULL;
    size_t n = kids ? rubraview_archive_index_level(ix, proven_u8str_view_from_cstr(rel), kids, ix->count) : 0;
    size_t have = level->listing.count;
    proven_result_mem_mut_t er = rubraview_arena_alloc_array(pa, have + n + 1, sizeof(rubraview_fs_entry_t));
    proven_result_mem_mut_t pr = rubraview_arena_alloc_array(pa, have + n + 1, sizeof(size_t));
    if (n > 0 && proven_is_ok(er.err) && proven_is_ok(pr.err)) {
        rubraview_fs_entry_t *e = (rubraview_fs_entry_t *)(void *)er.value.ptr;
        size_t *page_of = (size_t *)(void *)pr.value.ptr;
        if (have) {
            proven_sys_mem_copy(e, level->listing.entries, have * sizeof *e);
            proven_sys_mem_copy(page_of, level->page_of, have * sizeof *page_of);
        }
        size_t count = have;
        for (size_t i = 0; i < n; ++i) {
            bool listed = false;
            for (size_t k = 0; k < have && !listed; ++k)
                listed = e[k].is_directory == kids[i].is_dir && proven_u8str_view_eq(e[k].name, kids[i].name);
            if (listed) continue;
            proven_u8str_view_t full = rubraview_path_join(pa, dir, kids[i].name);
            e[count] = (rubraview_fs_entry_t){ .name = { .ptr = full.ptr + full.size - kids[i].name.size, .size = kids[i].name.size },
                                               .path = full, .size_bytes = kids[i].size, .mtime = kids[i].mtime,
                                               .ctime = kids[i].mtime, .is_directory = kids[i].is_dir };
            page_of[count++] = RUBRAVIEW_PICKER_NOT_A_PAGE;
        }
        level->listing = (rubraview_fs_listing_t){ .entries = e, .count = count };
        level->page_of = page_of;
    }
    rubraview_free(kids);
    rubraview_archive_index_free(ix);
    rubraview_free(ix);
}

static void picker_navigate_to(app_state_t *app, proven_u8str_view_t dir, bool from_history) {
    /* Where it was, kept before the listing's memory turns over; another folder goes on the way back. */
    char was[PICKER_HIST_PATH];
    size_t was_len = app->picker_dir.size < sizeof(was) ? app->picker_dir.size : 0;
    if (was_len) proven_sys_mem_copy(was, app->picker_dir.ptr, was_len);
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
    /* RV-207: inside the archive looked into, its whole tree; out of it, it is let go */
    bool in_arc = !places && !recent && app->picker_arc_path &&
                  rubraview_picker_inside_archive(dir, proven_u8str_view_from_cstr(app->picker_arc_path), NULL);
    if (!in_arc && app->picker_arc_path) picker_leave_archive(app);
    proven_u8str_view_t inner = PROVEN_LIT("");
    bool in_book = !in_arc && !places && !recent && app->source.kind != RUBRAVIEW_PAGE_SOURCE_FOLDER &&
                   rubraview_picker_inside_archive(dir, app->source.archive_path, &inner);
    rubraview_archive_level_t level = {0};
    if (in_book) {
        level = rubraview_picker_archive_level(pa, app->source.archive_path,
                                               app->source.pages, app->source.page_count, inner);
    }
    /* "." has no parent by name, which kept the reader below the folder the
       viewer was run from (owner, 2026-09-28): every folder is made whole. */
    if (!places && !in_book && !recent && !in_arc) dir = rubraview_pal_fs_absolute(pa, dir);
    rubraview_fs_listing_t listing = in_arc ? archive_listing(app, pa, dir)
                                   : in_book ? level.listing
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
            if (listing.count) proven_sys_mem_copy(all + app->favorites.count, listing.entries, listing.count * sizeof(rubraview_fs_entry_t));
            listing.entries = all;
            listing.count += app->favorites.count;
        }
    }
    /* D-258 (owner, 2026-11-02: "모든 파일이 다 보이게"): every file, a RAR set's later volumes too; what the viewer
       does not show opens in its own program (picker_activate). Was: only what the viewer opens (2026-09-21). */
    if (in_book && picker_every_file(app)) {
        book_listing_whole(app, pa, &level, inner, dir);
        listing = level.listing;
    }
    if (!picker_every_file(app) && !in_book && !in_arc && !places && !recent) {
        /* Viewer's files: folders and what the viewer opens, a RAR set by its first volume (owner, 2026-09-21, 2026-10-01) */
        size_t kept = 0;
        for (size_t i = 0; i < listing.count; ++i) {
            const rubraview_fs_entry_t *e = &listing.entries[i];
            if (!e->is_directory && (!picker_viewer_opens(e->name) || rubraview_page_source_is_later_volume(e->name))) continue;
            listing.entries[kept++] = *e;
        }
        listing.count = kept;
    }
    if (listing.count == 0 && rubraview_picker_parent(dir).size == 0) return;

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
            proven_sys_mem_copy(with_up + 1, listing.entries, listing.count * sizeof(rubraview_fs_entry_t));
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
    if (app->picker_selected) proven_sys_mem_set(app->picker_selected, 0, listing.count * sizeof(bool));

    rubraview_picker_mode_t mode = app->picker.mode;   /* the mode outlives the folder */
    picker_thumbs_free(app);
    app->picker_generation++;
    if (app->thumbs) rubraview_pal_thumbs_generation(app->thumbs, app->picker_generation);
    app->picker_thumbs = (struct picker_thumb *)rubraview_alloc_zero(listing.count ? listing.count : 1, sizeof(*app->picker_thumbs));
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
    if ((kept == RUBRAVIEW_PICKER_SORT_LENGTH || kept == RUBRAVIEW_PICKER_SORT_RESOLUTION) && !app->picker_in_book &&
        !rubraview_picker_is_places(app->picker_dir))
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
    if (picker_in_archive(app)) return 0;   /* RV-207: names inside an archive are no files to copy, move or recycle */
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
        int written = rubraview_fmt(name, sizeof(name), "Playlist{:04}.m3u8", PROVEN_ARG((int)number));
        if (written <= 0) return;
        proven_u8str_view_t candidate = rubraview_path_join(app->arena, app->picker_dir,
                                                (proven_u8str_view_t){ .ptr = (const proven_byte_t *)name, .size = (size_t)written });
        rubraview_fs_entry_t taken;
        if (!rubraview_pal_fs_stat(app->arena, candidate, &taken)) { target = candidate; break; }
    }
    if (target.size == 0) { osd_say(app, TU8("too many playlists in this folder")); return; }
    if (!rubraview_pal_fs_write_file(target, text)) { osd_say(app, TU8("could not write the playlist")); return; }

    char line[96];
    int written = rubraview_fmt(line, sizeof(line), T("{} file(s) in {}"), PROVEN_ARG((size_t)n), PROVEN_ARG(name));
    open_set_from_entries(app, entries, n, written > 0 ? (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)written }
                                                       : TU8("playlist opened"));
}

/* RV-207 (owner, 2026-11-01): a new folder in the folder on screen ("New folder", or New folder-1 when that is
   there), its name then typed in the rename box. */
void picker_new_folder(app_state_t *app) {
    if (app->picker_dir.size == 0 || rubraview_picker_is_places(app->picker_dir) || rubraview_picker_is_recent(app->picker_dir) ||
        app->picker_in_book || picker_in_archive(app)) {
        osd_say(app, TU8("no new folder can be made here"));
        return;
    }
    proven_u8str_view_t want = rubraview_path_join(app->arena, app->picker_dir, proven_u8str_view_from_cstr(T("New folder")));
    char made[RUBRAVIEW_ARCHIVE_PATH];
    if (want.size == 0 || !rubraview_archive_free_folder_name(want, rubraview_pal_fs_exists, made, sizeof(made)) ||
        !rubraview_pal_fs_make_dirs(proven_u8str_view_from_cstr(made))) {
        osd_say(app, TU8("could not make a new folder here"));
        return;
    }
    picker_navigate(app, app->picker_dir);   /* it is listed now */
    proven_u8str_view_t name = rubraview_path_basename(proven_u8str_view_from_cstr(made));
    for (size_t i = 0; i < app->picker_listing.count; ++i) {
        if (!proven_u8str_view_eq(app->picker_listing.entries[i].name, name)) continue;
        app->picker.focus = i;
        rubraview_picker_reveal_focus(&app->picker);
        rename_picker_begin(app, i);
        return;
    }
    osd_say(app, TU8("a new folder is made"));   /* hidden by the filter: made, not renamed */
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
        int written = rubraview_fmt(line, sizeof(line), T("press Recycle again to bin {} file(s)"), PROVEN_ARG((size_t)n));
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
    int written = rubraview_fmt(line, sizeof(line), T("{} file(s) in the recycle bin"), PROVEN_ARG((size_t)done));
    if (written > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)written });
    picker_navigate(app, app->picker_dir);
}

/* RV-207 (owner, 2026-11-01): Ctrl+C / Ctrl+X put what is picked (nothing picked, the tile in focus) on the
   clipboard as Explorer does, so Explorer pastes it too; Ctrl+V puts what is there — from here or from Explorer —
   into the folder on screen: a name there already gets _2 (RV-258, D-310), a cut is then cleared, one undo takes all of it back. */
void picker_clip_put(app_state_t *app, bool moving) {
    if (picker_in_archive(app)) { picker_clip_put_archive(app, moving); return; }   /* RV-223 */
    rubraview_fs_entry_t *entries = NULL;
    size_t n = picker_picked_entries(app, &entries);
    rubraview_fs_entry_t one;
    if (n == 0 && !app->picker_in_book && app->picker.focus < app->picker_listing.count && !picker_is_up(app, app->picker.focus) &&
        !rubraview_picker_is_places(app->picker_dir)) {
        one = app->picker_listing.entries[app->picker.focus];
        entries = &one;
        n = 1;
    }
    if (n == 0) { osd_say(app, TU8("pick some files first")); return; }
    proven_result_mem_mut_t res = rubraview_arena_alloc_array(app->arena, n, sizeof(proven_u8str_view_t));
    if (!proven_is_ok(res.err)) return;
    proven_u8str_view_t *paths = (proven_u8str_view_t*)(void*)res.value.ptr;
    for (size_t i = 0; i < n; ++i) paths[i] = entries[i].path;
    if (!rubraview_pal_clipboard_set_files(app->window, paths, n, moving)) { osd_say(app, TU8("the clipboard would not take it")); return; }
    char line[128];
    int written = rubraview_fmt(line, sizeof(line), moving ? T("{} to move: Ctrl+V in another folder") : T("{} to copy: Ctrl+V where they go"), PROVEN_ARG((size_t)n));
    if (written > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)written });
}

static bool path_is_there(proven_u8str_view_t path) { return rubraview_pal_fs_exists(path); }

/* Copies or moves `sources` into `dir` as a paste does: a name there already gets _2 (D-310), a folder into itself or a cut
   into its own folder is skipped, a folder moved to another drive is copied and the one left recycled; one undo for
   all. `moved[i]` (when given) says which arrived. Returns how many did. */
size_t paste_paths(app_state_t *app, proven_u8str_view_t dir, const proven_u8str_view_t *sources, size_t n, bool moving,
                   bool *moved, size_t *skipped, size_t *failed) {
    size_t done = 0;
    uint32_t batch = ++app->undo_batch;
    for (size_t i = 0; i < n; ++i) {
        if (moved) moved[i] = false;
        proven_u8str_view_t source = sources[i];
        rubraview_fs_entry_t entry;
        if (!rubraview_pal_fs_stat(app->arena, source, &entry)) { (*failed)++; continue; }   /* gone since it was copied */
        rubraview_paste_check_t check = rubraview_paste_check(source, dir, moving);
        if (check != RUBRAVIEW_PASTE_GO) { (*skipped)++; continue; }
        proven_u8str_view_t want = rubraview_path_join(app->arena, dir, rubraview_path_basename(source));
        char target[RUBRAVIEW_ARCHIVE_PATH];
        bool named = want.size > 0 && (entry.is_directory ? rubraview_archive_free_folder_name(want, path_is_there, target, sizeof(target))
                                                          : rubraview_archive_free_name(want, path_is_there, target, sizeof(target)));
        if (!named) { (*failed)++; continue; }
        proven_u8str_view_t to = app_keep(app, proven_u8str_view_from_cstr(target));
        bool ok;
        if (moving) {
            ok = rubraview_pal_fs_move(source, to);
            /* a folder to another drive: copied whole, then the one left behind to the recycle bin */
            if (!ok && entry.is_directory && rubraview_pal_fs_copy_tree(source, to)) ok = rubraview_pal_fs_recycle(source);
        } else {
            ok = entry.is_directory ? rubraview_pal_fs_copy_tree(source, to) : rubraview_pal_fs_copy(source, to);
        }
        if (!ok) { (*failed)++; continue; }
        rubraview_undo_push(&app->undo, (rubraview_file_action_t){
            .op = moving ? RUBRAVIEW_FILE_OP_MOVE : RUBRAVIEW_FILE_OP_COPY,
            .source_path = app_keep(app, source),
            .target_path = to,
            .batch = batch,
        });
        if (moved) moved[i] = true;
        done++;
    }
    return done;
}

void picker_clip_paste(app_state_t *app) {
    proven_u8str_view_t dir = app->picker_dir;
    if (dir.size == 0 || rubraview_picker_is_places(dir) || rubraview_picker_is_recent(dir) || app->picker_in_book || picker_in_archive(app)) {
        osd_say(app, TU8("nothing can be pasted here"));
        return;
    }
    enum { CLIP_BYTES = 1u << 20 };
    char *buf = (char*)rubraview_alloc(CLIP_BYTES);
    if (!buf) return;
    bool moving = false;
    size_t count = rubraview_pal_clipboard_get_files(app->window, buf, CLIP_BYTES, &moving);
    if (count == 0) { rubraview_free(buf); osd_say(app, TU8("no files on the clipboard: Ctrl+C or Ctrl+X them first")); return; }
    proven_result_mem_mut_t res = rubraview_arena_alloc_array(app->arena, count, sizeof(proven_u8str_view_t));
    if (!proven_is_ok(res.err)) { rubraview_free(buf); return; }
    proven_u8str_view_t *sources = (proven_u8str_view_t*)(void*)res.value.ptr;
    const char *at = buf;
    for (size_t i = 0; i < count; ++i, at += proven_cstr_len(at) + 1) sources[i] = proven_u8str_view_from_cstr(at);
    size_t skipped = 0, failed = 0;
    size_t done = paste_paths(app, dir, sources, count, moving, NULL, &skipped, &failed);
    rubraview_free(buf);
    if (moving && done > 0) dragout_clip_pasted(app);   /* RV-223: a cut out of an archive, pasted here */
    if (moving && done > 0 && failed == 0) rubraview_pal_clipboard_clear(app->window);   /* as Explorer: a cut is pasted once */
    char line[160];
    int written = failed > 0 ? rubraview_fmt(line, sizeof(line), T("{} pasted, {} could not be"), PROVEN_ARG((size_t)done), PROVEN_ARG((size_t)failed))
                : skipped > 0 && done == 0 ? rubraview_fmt(line, sizeof(line), "{}", PROVEN_ARG(T("already here, or a folder into itself: nothing pasted")))
                : rubraview_fmt(line, sizeof(line), moving ? T("{} moved here") : T("{} copied here"), PROVEN_ARG((size_t)done));
    if (written > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)written });
    if (done > 0) picker_navigate(app, dir);
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
    int written = rubraview_fmt(line, sizeof(line), T("{} file(s) {} {}"), PROVEN_ARG((size_t)done), PROVEN_ARG(moving ? T("moved to") : T("copied to")),
                           PROVEN_ARG(rubraview_bytes_view(target_dir.ptr, (int)target_dir.size)));
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

/* ---- RV-227 (D-282; owner, 2026-11-06: "여러개 중 하나의 장바구니에 담는 기능"): five baskets of paths ---- */

size_t basket_total(const app_state_t *app) {
    size_t n = 0;
    for (int i = 0; i < RUBRAVIEW_BASKETS; ++i) n += rubraview_basket_count(&app->baskets, i);
    return n;
}

/* The basket button's upper part: what is picked waits for a basket chosen in the list. A drag let go on the button (dragout.c) sets the waiting paths itself. */
void basket_add_picked(app_state_t *app, double x, double y, double above_y) {
    rubraview_fs_entry_t *entries = NULL;
    size_t n = picker_in_archive(app) ? 0 : picker_picked_entries(app, &entries);
    if (n == 0 && picker_in_archive(app) && picker_picked_count(app) > 0) {
        osd_say(app, TU8("inside an archive: extract them first, then put them in a basket"));
        return;
    }
    if (n == 0) {   /* D-286: nothing picked, the baskets to see */
        if (basket_total(app) == 0) { osd_say(app, TU8("the baskets are empty: pick files, then the Basket button")); return; }
        app->basket_pending_count = 0;
        chooser_open_basket(app, BASKET_LIST_SEE, x, y, above_y);
        return;
    }
    proven_result_mem_mut_t res = rubraview_arena_alloc_array(app->arena, n, sizeof(proven_u8str_view_t));
    if (!proven_is_ok(res.err)) return;
    app->basket_pending = (proven_u8str_view_t*)(void*)res.value.ptr;
    for (size_t i = 0; i < n; ++i) app->basket_pending[i] = app_keep(app, entries[i].path);
    app->basket_pending_count = n;
    chooser_open_basket(app, BASKET_LIST_PUT_IN, x, y, above_y);
}

/* The arrow: a basket's files into the folder on screen. */
void basket_put_open(app_state_t *app, double x, double y, double above_y) {
    proven_u8str_view_t dir = app->picker_dir;
    if (dir.size == 0 || rubraview_picker_is_places(dir) || rubraview_picker_is_recent(dir) || picker_in_archive(app)) {
        osd_say(app, TU8("go to the folder they go into, then the arrow"));
        return;
    }
    if (basket_total(app) == 0) { osd_say(app, TU8("the baskets are empty: pick files, then the Basket button")); return; }
    app->basket_pending_count = 0;
    app->basket_moving = false;   /* Copy here each time it opens (the owner's answer) */
    chooser_open_basket(app, BASKET_LIST_PUT_HERE, x, y, above_y);
}

/* A basket chosen in the list: the waiting paths into it, or (from the arrow) its files here. */
void basket_chosen(app_state_t *app, int which) {
    if (app->basket_pending_count > 0) {
        size_t n = app->basket_pending_count;
        size_t added = rubraview_basket_add(&app->baskets, which, app->basket_pending, n);
        app->basket_pending_count = 0;
        app->basket_pending = NULL;
        char line[160];
        rubraview_fmt(line, sizeof line, T("basket {}: {} put in, {} in it"), PROVEN_ARG(which + 1), PROVEN_ARG(added),
                      PROVEN_ARG(rubraview_basket_count(&app->baskets, which)));
        osd_say(app, cstr(line));
        return;
    }
    size_t n = rubraview_basket_count(&app->baskets, which);
    if (n == 0) { osd_say(app, TU8("that basket is empty")); return; }
    proven_result_mem_mut_t res = rubraview_arena_alloc_array(app->arena, n, sizeof(proven_u8str_view_t));
    proven_result_mem_mut_t mres = rubraview_arena_alloc_array(app->arena, n, sizeof(bool));
    if (!proven_is_ok(res.err) || !proven_is_ok(mres.err)) return;
    proven_u8str_view_t *sources = (proven_u8str_view_t*)(void*)res.value.ptr;
    bool *moved = (bool*)(void*)mres.value.ptr;
    for (size_t i = 0; i < n; ++i) sources[i] = app_keep(app, rubraview_basket_path(&app->baskets, which, i));
    bool moving = app->basket_moving;
    proven_u8str_view_t dir = app->picker_dir;
    size_t skipped = 0, failed = 0;
    size_t done = paste_paths(app, dir, sources, n, moving, moved, &skipped, &failed);
    if (moving) rubraview_basket_keep_unmoved(&app->baskets, which, moved);   /* a copy keeps the basket */
    char line[200];
    if (failed > 0 || skipped > 0)
        rubraview_fmt(line, sizeof line, moving ? T("basket {}: {} moved here, {} not (gone, or already here)") : T("basket {}: {} copied here, {} not (gone, or already here)"),
                      PROVEN_ARG(which + 1), PROVEN_ARG(done), PROVEN_ARG(failed + skipped));
    else
        rubraview_fmt(line, sizeof line, moving ? T("basket {}: {} moved here  (Ctrl+Z takes it back)") : T("basket {}: {} copied here  (Ctrl+Z takes it back)"),
                      PROVEN_ARG(which + 1), PROVEN_ARG(done));
    osd_say(app, cstr(line));
    if (done > 0) picker_navigate(app, dir);
}
