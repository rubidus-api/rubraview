/*
 * The settings window (its file, rows, tables and queue), the folders kept out, a folder typed in, the font picker
 * and the one-of-many list (RV-163, D-197: moved from main.c as it was).
 */
#include "app.h"

/* ---- the settings window (§3.22), RV-082, D-13 ---- */

/* §3.22.1 asks for a separate top-level window, and since D-13 that is
   what it is: a window owned by the viewer's, drawn by its own renderer,
   pumped by the same loop. What is on it is not written here control by
   control — the settings document says what each page holds, the
   interpreter (ui_settings) lays it out on a grid of fixed-width cells
   and turns keys and clicks into changes, and this code only prints the
   lines it is given and does what an event names.

   A change applies to the viewer at once; the file is written when the
   window closes; Revert returns to what the file held when it opened
   (owner, 2026-09-13, "1a"). */

/* §3.22.1: reads settings.ini — portable mode beside the executable,
   otherwise under AppData, the same rule the reading history follows,
   so the two files never end up in different places.

   This runs at startup, not only when the settings window is opened.
   It used to run only there, which meant a settings.ini on disk changed
   nothing until the reader pressed F10: every setting read while
   viewing came back 0. */
void settings_read_file(app_state_t *app) {
    /* §3.17.2: a settings.ini *beside the executable* means portable mode,
       and then nothing is written to the host machine. Beside the
       executable, not in the folder the viewer was started in: until
       2026-09-23 this asked for the plain name `settings.ini`, so both
       portable mode and §3.18.3's triage folders followed the working
       directory — which is why the number keys curated nothing. */
    proven_u8str_view_t exe = rubraview_pal_process_executable(app->arena);
    proven_u8str_view_t beside = exe.size > 0 ? rubraview_path_dirname(exe) : PROVEN_LIT(".");
    if (beside.size == 0) beside = PROVEN_LIT(".");
    proven_u8str_view_t portable_ini = rubraview_path_join(app->arena, beside, PROVEN_LIT("settings.ini"));
    app->config_mode = rubraview_config_mode_for(portable_ini.size > 0 &&
                                                 rubraview_pal_fs_exists(portable_ini));
    app->config_beside = beside;
    app->packaged = rubraview_pal_process_packaged(app->package_family, sizeof(app->package_family));
    app->other_copies = rubraview_pal_shell_other_copies();

    proven_u8str_view_t appdata = PROVEN_LIT(".");
#ifdef _WIN32
    char appdata_utf8[1024];
    DWORD written = GetEnvironmentVariableA("APPDATA", appdata_utf8, (DWORD)sizeof(appdata_utf8));
    if (written > 0 && written < sizeof(appdata_utf8)) {
        appdata = (proven_u8str_view_t){ .ptr = (const proven_byte_t *)appdata_utf8, .size = written };
    }
#endif
    app->settings_path = rubraview_config_path(app->arena, app->config_mode,
                                               beside, appdata, PROVEN_LIT("settings.ini"));
    app->keymap_path = rubraview_config_path(app->arena, app->config_mode,
                                             beside, appdata, PROVEN_LIT("keymap.ini"));

    proven_u8str_view_t text = rubraview_pal_fs_read_file(app->arena, app->settings_path, 256u * 1024u);
    app->settings = rubraview_settings_load(app->arena, text);
    shell_menu_take_old(app, text);
    app->settings_saved = app->settings;
    skip_dirs_take_old(app);
}

/* RV-184: the Keys table, by function (headings and bindings) or by key */
#define KEY_ROWS_MAX 1024
typedef struct { size_t binding, combo; } key_combo_row_t;
static bool g_keys_by_key;
static int32_t g_key_row[KEY_ROWS_MAX];          /* by function: a binding, or -(context + 1) for a heading */
static key_combo_row_t g_key_combo[KEY_ROWS_MAX];  /* by key: where each key was first met */
static size_t g_key_row_count;

/* RV-185: the folder rubraview.exe runs from, with Windows' separator. */
static proven_u8str_view_t program_folder(app_state_t *app) {
    proven_u8str_view_t exe = rubraview_pal_process_executable(app->arena);
    proven_u8str_view_t dir = rubraview_path_dirname(exe);
    proven_result_mem_mut_t m = proven_arena_alloc(app->arena, dir.size + 1);
    if (dir.size == 0 || !proven_is_ok(m.err)) return PROVEN_LIT("");
    for (size_t i = 0; i < dir.size; ++i) m.value.ptr[i] = dir.ptr[i] == '/' ? '\\' : dir.ptr[i];
    m.value.ptr[dir.size] = '\0';
    return (proven_u8str_view_t){ .ptr = m.value.ptr, .size = dir.size };
}

/* The place named for G'MIC: a folder, or the folder of the .exe named. */
static proven_u8str_view_t gmic_named_folder(app_state_t *app) {
    proven_u8str_view_t where = rubraview_pal_fs_expand_env(app->arena, rubraview_path_typed_folder(cstr(g_gmic_where)));
    if (where.size > 4 && _strnicmp((const char *)where.ptr + where.size - 4, ".exe", 4) == 0) where = rubraview_path_dirname(where);
    return where;
}

/* RV-185: G'MIC-Qt's stand-alone download is the one most often taken by mistake; it has no gmic.exe. */
static bool gmic_qt_named(app_state_t *app) {
    proven_u8str_view_t dir = gmic_named_folder(app);
    return dir.size > 0 && rubraview_pal_fs_exists(rubraview_path_join(app->arena, dir, PROVEN_LIT("gmic_qt.exe")));
}


#define SETTINGS_BACKGROUND  0xFF161616u
#define SETTINGS_PANE        0xFF1E1E1Eu
#define SETTINGS_FOCUS       0xFF2D4A6Eu
#define SETTINGS_TEXT        0xFFE8E8E8u
#define SETTINGS_DIM         0xFF8A8A8Au   /* a setting nothing reads yet (not `wired`) */
#define SETTINGS_LINK        0xFF5AAEFFu   /* what can be pressed: an action, a button, a path's box, underlined */

/* The live values the document's `info` lines name. */
static size_t settings_info(void *user, proven_u8str_view_t source, char *buffer, size_t capacity) {
    app_state_t *app = (app_state_t*)user;
    int n = 0;
    if (proven_u8str_view_eq(source, PROVEN_LIT("config.path"))) {
        proven_u8str_view_t shown = app->settings_path;
        char real[1024];
        if (app->packaged && app->package_family[0]) {
            /* RV-113 (2): a packaged program's AppData is its package's
               LocalCache; the path it uses is not the one Explorer shows. */
            char local[512];
            DWORD got = GetEnvironmentVariableA("LOCALAPPDATA", local, (DWORD)sizeof(local));
            if (got > 0 && got < sizeof(local)) {
                int m = snprintf(real, sizeof(real), "%s\\Packages\\%s\\LocalCache\\Roaming\\Rubraview\\settings.ini", local, app->package_family);
                proven_u8str_view_t candidate = { .ptr = (const proven_byte_t *)real, .size = m > 0 && (size_t)m < sizeof(real) ? (size_t)m : 0 };
                if (candidate.size > 0 && rubraview_pal_fs_exists(candidate)) shown = candidate;
            }
        }
        n = snprintf(buffer, capacity, "%.*s%s", (int)shown.size, shown.ptr,
                     app->config_mode == RUBRAVIEW_CONFIG_PORTABLE ? T("  (portable)") : "");
        /* The folder comes from Windows with backslashes and the file name is
           joined with '/': shown with Windows' own separator throughout. */
        for (int i = 0; i < n && (size_t)i < capacity; i++) if (buffer[i] == '/') buffer[i] = '\\';
    } else if (proven_u8str_view_eq(source, PROVEN_LIT("app.others"))) {
        /* D-154: two copies, two menus: what this machine has besides this one. */
        const rubraview_other_copies_t *o = &app->other_copies;
        n = snprintf(buffer, capacity, "%s",
                     o->msi && o->store ? T("the MSI's and the Store's too: Windows 11's menu may show twice")
                     : o->store ? T("the Microsoft Store's too: Windows 11's menu is left to it")
                     : o->msi && o->msi_menu && app->packaged ? T("the MSI's too, with its menu: Windows 11's menu shows twice (Remove for every user in that copy)")
                     : o->msi ? T("the one the MSI installed too")
                     : T("none"));
    } else if (proven_u8str_view_eq(source, PROVEN_LIT("app.folder"))) {   /* RV-185: where this copy runs from */
        proven_u8str_view_t dir = program_folder(app);
        n = snprintf(buffer, capacity, "%.*s", (int)dir.size, (const char *)dir.ptr);
    } else if (proven_u8str_view_eq(source, PROVEN_LIT("app.kind"))) {
        n = snprintf(buffer, capacity, "%s",
                     app->packaged ? T("the Microsoft Store's: Windows has its file types from the package")
                     : app->config_mode == RUBRAVIEW_CONFIG_PORTABLE ? T("portable: the settings beside it")
                     : T("from the zip or the installer: register below"));
    } else if (proven_u8str_view_eq(source, PROVEN_LIT("media.ffmpeg"))) {
        int place = rubraview_pal_ffmpeg_place();
        /* RFC-0009: found where it is named, else beside the program, else where an older Import put it. */
        n = snprintf(buffer, capacity, "%s",
                     place == 3 ? T("its DLLs are in the FFmpeg folder")
                     : place == 1 ? T("its DLLs are beside the program")
                     : place == 2 ? T("its DLLs are where an older version's Import put them")
                     : T("not found (or not 9.x) - Media Foundation only"));
        char encoders[96];   /* RV-139: what it can write pictures with */
        if (place > 0 && n > 0 && (size_t)n < capacity && rubraview_pal_ffmpeg_image_encoders(encoders, sizeof(encoders)) && encoders[0]) {
            int m = snprintf(buffer + n, capacity - (size_t)n, T("; writes AVIF / JPEG XL with %s"), encoders);
            if (m > 0) n += m;
        }
    } else if (proven_u8str_view_eq(source, PROVEN_LIT("ffmpeg.filters"))) {   /* RFC-0009 D */
        rubraview_ffilter_state_t s = rubraview_pal_ffilter_state();
        if (s == RUBRAVIEW_FFILTER_READY && ffmpeg_list_ready())
            n = snprintf(buffer, capacity, T("%d filters, under Edit"), g_ffm.filter_count);
        else
            n = snprintf(buffer, capacity, "%s",
                         s == RUBRAVIEW_FFILTER_NO_FFMPEG ? T("none: FFmpeg is not found")
                         : s == RUBRAVIEW_FFILTER_OTHER_VERSION ? T("none: its avfilter DLL is not FFmpeg 9.0's")
                         : s == RUBRAVIEW_FFILTER_READY ? T("none: avfilter lists no filter")
                         : T("none: avfilter-12.dll is not beside FFmpeg's other DLLs"));
    } else if (proven_u8str_view_eq(source, PROVEN_LIT("gmic.status"))) {   /* RFC-0009 C */
        if (!g_gmic_exe[0]) n = snprintf(buffer, capacity, "%s", !g_gmic_where[0] ? T("not set: name gmic.exe or its folder below")
                                                              : gmic_qt_named(app) ? T("that is G'MIC-Qt's stand-alone: the command-line (CLI) zip holds gmic.exe")
                                                              : T("gmic.exe is not in the place named: the command-line (CLI) zip holds it"));
        else if (g_gmic_job) n = (int)gmic_list_progress(buffer, capacity, app);   /* RV-186 */
        else if (g_gmic.filter_count > 0)
            n = snprintf(buffer, capacity, T("found; %d filters in %d categories (G'MIC %.*s), under Edit"), g_gmic.filter_count,
                         g_gmic.category_count, (int)g_gmic.version.size, (const char *)g_gmic.version.ptr);
        else n = snprintf(buffer, capacity, T("found; %s"), g_gmic_list_why ? rubraview_tr(g_gmic_list_why) : T("no filter list yet"));
    } else if (proven_u8str_view_eq(source, PROVEN_LIT("gpu.adapter"))) {
        n = snprintf(buffer, capacity, T("run rubraview --probe-gpu <video> for the details"));
    } else if (proven_u8str_view_eq(source, PROVEN_LIT("memory.use")) && app->mem) {
        /* RFC-0006: the program's memory by use, and the most the session ever held */
        rubraview_mem_stats_t st = rubraview_mem_stats(app->mem);
        n = snprintf(buffer, capacity, T("session %zu MB (most %zu), a track %zu MB, %zu window(s), %zu MB for the moment; %zu MB held"),
                     st.persist_used >> 20, st.persist_peak >> 20, st.media_used >> 20, st.windows_live, st.scratch_live >> 20,
                     st.total_held >> 20);
    } else if (proven_u8str_view_eq(source, PROVEN_LIT("cache.used"))) {
        size_t loaded = 0;
        uint64_t bytes = 0;
        for (size_t i = 0; i < page_count(app); ++i) {
            if (!app->pages[i].texture) continue;
            loaded++;
            bytes += (uint64_t)app->pages[i].width * (uint64_t)app->pages[i].height * 4u;
        }
        /* RV-107: and how much that is of the cap. */
        n = snprintf(buffer, capacity, T("%zu page(s), about %llu MB of %zu MB"), loaded,
                     (unsigned long long)((bytes + 524288u) / 1048576u),
                     (app->cache_budget > 0 ? app->cache_budget : PAGE_CACHE_BUDGET) / 1048576u);
    }
    if (n <= 0) return 0;
    return (size_t)n < capacity ? (size_t)n : capacity - 1;
}

/* The rows the document's `table` lines name. */
/* ---- The folders kept out of Recent, as many as wanted (owner, 2026-10-16) ---- */

/* The lists typed a folder a row, each a table of its own: the folders kept
   out of Recent, and (owner, 2026-10-18) the folders shown without thumbnails. */
enum { FOLDER_LIST_RECENT = 0, FOLDER_LIST_THUMBS, FOLDER_LIST_COUNT };
static const struct { const char *table, *section, *key; } FOLDER_LISTS[FOLDER_LIST_COUNT] = {
    { "skip_dirs",  "recent", "skip_dirs" },
    { "thumb_dirs", "thumbs", "skip_dirs" },
};

static proven_u8str_view_t folder_list_text(const app_state_t *app, int32_t list) {
    return rubraview_settings_get_text(&app->settings, cstr(FOLDER_LISTS[list].section), cstr(FOLDER_LISTS[list].key));
}

static void folder_list_set(app_state_t *app, int32_t list, proven_u8str_view_t text) {
    rubraview_settings_set_text(&app->settings, cstr(FOLDER_LISTS[list].section), cstr(FOLDER_LISTS[list].key), text);
}

/* The list a table shows, or -1. */
static int32_t folder_list_of_table(proven_u8str_view_t table) {
    for (int32_t l = 0; l < FOLDER_LIST_COUNT; ++l) if (proven_u8str_view_eq(table, proven_u8str_view_from_cstr(FOLDER_LISTS[l].table))) return l;
    return -1;
}

static size_t folder_list_count(const app_state_t *app, int32_t list) {
    return rubraview_folder_list_split(folder_list_text(app, list), NULL, 0);
}

/* Each table's height: its heading, a row a folder, and one to add another. */
void skip_dirs_table_rows(app_state_t *app) {
    for (int32_t l = 0; l < FOLDER_LIST_COUNT; ++l) {
        rubraview_settings_view_set_named_table_rows(&app->settings_view, cstr(FOLDER_LISTS[l].table), (int32_t)folder_list_count(app, l) + 2);
    }
    /* RFC-0007: the plug-ins' heading and one row each (or one that says why there are none) */
    rubraview_settings_view_set_named_table_rows(&app->settings_view, PROVEN_LIT("plugins"), (int32_t)(g_plugin_count > 0 ? g_plugin_count : 1) + 1);
    /* RV-186: G'MIC's list made, a row a category */
    rubraview_settings_view_set_named_table_rows(&app->settings_view, PROVEN_LIT("gmic_filters"), (g_gmic.category_count > 0 ? g_gmic.category_count : 1) + 1);
}

/* The eight folders of before go into the list, once, and are emptied in the file. */
/* RV-120: a settings file from before each extension had its own menu box
   has no shell.menu_<ext>; the menu box starts as that extension's Open
   with box was, so registering again changes nothing by itself. */
void shell_menu_take_old(app_state_t *app, proven_u8str_view_t text) {
    rubraview_ini_doc_t ini = rubraview_ini_parse(app->arena, text);
    size_t count = 0;
    const rubraview_setting_def_t *schema = rubraview_settings_schema(&count);
    for (size_t i = 0; i < count; ++i) {
        const rubraview_setting_def_t *def = &schema[i];
        if (def->pair == 0 || !proven_u8str_view_eq(def->section, PROVEN_LIT("shell"))) continue;
        const rubraview_setting_def_t *menu = &schema[def->pair];
        if (rubraview_ini_get(&ini, menu->section, menu->key)) continue;
        rubraview_settings_set(&app->settings, menu->section, menu->key, rubraview_settings_get(&app->settings, def->section, def->key));
    }
}

void skip_dirs_take_old(app_state_t *app) {
    static const char *const OLD[8] = { "skip_dir_1", "skip_dir_2", "skip_dir_3", "skip_dir_4",
                                        "skip_dir_5", "skip_dir_6", "skip_dir_7", "skip_dir_8" };
    bool moved = false;
    for (int i = 0; i < 8; ++i) {
        proven_u8str_view_t key = { .ptr = (const proven_byte_t *)OLD[i], .size = strlen(OLD[i]) };
        proven_u8str_view_t folder = rubraview_settings_get_text(&app->settings, PROVEN_LIT("recent"), key);
        if (folder.size == 0) continue;
        char buffer[8192];
        proven_u8str_view_t list = rubraview_folder_list_put(buffer, sizeof(buffer), rubraview_settings_get_text(&app->settings, PROVEN_LIT("recent"), PROVEN_LIT("skip_dirs")),
                                                 folder, (size_t)-1);
        if (list.size == 0) continue;
        rubraview_settings_set_text(&app->settings, PROVEN_LIT("recent"), PROVEN_LIT("skip_dirs"), app_keep(app, list));
        rubraview_settings_set_text(&app->settings, PROVEN_LIT("recent"), key, PROVEN_LIT(""));
        moved = true;
    }
    if (moved) settings_write_if_changed(app, false);
}

static size_t settings_table_row(void *user, proven_u8str_view_t source, size_t index, char *buffer, size_t capacity) {
    app_state_t *app = (app_state_t*)user;
    int32_t folders = folder_list_of_table(source);
    if (folders >= 0) {
        proven_u8str_view_t dirs[256];
        size_t n = rubraview_folder_list_split(folder_list_text(app, folders), dirs, 256);
        /* The last row is the box every folder is typed in; the one being changed says so. */
        bool typing = app->spath_active && app->spath_list_index >= 0 && app->spath_list == folders;
        bool changing = typing && (size_t)app->spath_list_index == index - 1;
        int w = index == 0 ? snprintf(buffer, capacity, T("Folder  (Enter changes it in the box below; Delete or the x at the end takes it off)"))
              : index <= n ? snprintf(buffer, capacity, "%s%.*s%s", changing ? "> " : "", (int)dirs[index - 1].size, dirs[index - 1].ptr,
                                      changing ? T("   (being changed below)") : "")
              : index == n + 1 ? (typing ? 0
                                  : snprintf(buffer, capacity, T(" type or paste a folder to add  (Enter or a click; Ctrl+O browses)")))
              : 0;
        return w > 0 ? (size_t)w < capacity ? (size_t)w : capacity - 1 : 0;
    }
    if (proven_u8str_view_eq(source, PROVEN_LIT("gmic_filters"))) {   /* RV-186: the list G'MIC made, a row a category */
        int w = 0;
        if (index == 0) w = snprintf(buffer, capacity, "%s", T("Category (filters)  -  the first of them"));
        else if (g_gmic.category_count == 0) w = index == 1 ? snprintf(buffer, capacity, "%s", T("no list yet: Make the filter list above")) : 0;
        else if ((int32_t)index <= g_gmic.category_count) {
            int32_t c = (int32_t)index - 1, count = 0;
            for (int32_t i = 0; i < g_gmic.filter_count; ++i) count += g_gmic.filters[i].category == c;
            w = snprintf(buffer, capacity, "%.*s (%d)  - ", (int)g_gmic.categories[c].size, (const char *)g_gmic.categories[c].ptr, (int)count);
            bool first = true;
            for (int32_t i = 0; i < g_gmic.filter_count && w > 0 && (size_t)w + 8 < capacity; ++i) {
                if (g_gmic.filters[i].category != c) continue;
                int m = snprintf(buffer + w, capacity - (size_t)w, "%s%.*s", first ? " " : ", ", (int)g_gmic.filters[i].name.size,
                                 (const char *)g_gmic.filters[i].name.ptr);
                if (m <= 0 || (size_t)m >= capacity - (size_t)w) break;
                w += m;
                first = false;
            }
        }
        return w > 0 ? (size_t)w < capacity ? (size_t)w : capacity - 1 : 0;
    }
    if (proven_u8str_view_eq(source, PROVEN_LIT("plugins"))) {   /* RFC-0007: what the folder holds */
        int w = 0;
        bool on = rubraview_settings_get(&app->settings, PROVEN_LIT("edit"), PROVEN_LIT("plugins")) > 0.5;
        if (index == 0) w = snprintf(buffer, capacity, "%s", T("Plug-in  (version, licence)  -  file"));
        else if (!on) w = index == 1 ? snprintf(buffer, capacity, "%s", T("plug-ins are off")) : 0;
        else if (g_plugin_count == 0) w = index == 1 ? snprintf(buffer, capacity, T("none in %s"), g_plugin_folder) : 0;
        else if (index <= g_plugin_count) {
            const rubraview_plugin_t *p = &g_plugins[index - 1];
            w = p->usable ? snprintf(buffer, capacity, "%s  (%s, %s)  -  %s", p->name, p->version[0] ? p->version : "?",
                                     p->licence[0] ? p->licence : T("licence not said"), p->file)
                          : snprintf(buffer, capacity, T("%s  -  not used: %s"), p->file, p->why ? rubraview_tr(p->why) : "?");
        }
        return w > 0 ? (size_t)w < capacity ? (size_t)w : capacity - 1 : 0;
    }
    if (!proven_u8str_view_eq(source, PROVEN_LIT("keymap"))) return 0;
    if (index == 0) {
        int n = g_keys_by_key ? snprintf(buffer, capacity, "%-16s %s", T("key"), T("what it does, and where  (Show by function to change them)"))
                              : snprintf(buffer, capacity, "%-24s %s", T("action"), T("keys  (Enter adds a key, Delete takes the last off)"));
        return n > 0 ? (size_t)n < capacity ? (size_t)n : capacity - 1 : 0;
    }
    if (index > g_key_row_count) return 0;
    if (g_keys_by_key) {   /* RV-184: a key, and what it does in each context that binds it */
        const rubraview_key_binding_t *first = &app->keymap.bindings[g_key_combo[index - 1].binding];
        rubraview_key_combo_t combo = first->combos[g_key_combo[index - 1].combo];
        char one[64];
        proven_u8str_view_t named = rubraview_key_combo_format(one, sizeof(one), combo);
        int n = snprintf(buffer, capacity, "%-16.*s", (int)named.size, (const char *)named.ptr);
        bool more = false;
        for (size_t e = g_key_combo[index - 1].binding; e < app->keymap.count && n > 0 && (size_t)n + 8 < capacity; ++e) {
            const rubraview_key_binding_t *o = &app->keymap.bindings[e];
            bool binds = false;
            for (size_t f = 0; f < o->combo_count && !binds; ++f) binds = rubraview_key_combo_equal(o->combos[f], combo);
            if (!binds) continue;
            const rubraview_key_context_info_t *info = rubraview_keymap_context_info(o->context);
            const char *words = rubraview_action_words(o->action);
            int m = words ? snprintf(buffer + n, capacity - (size_t)n, "%s%s (%s)", more ? "  ·  " : " ", rubraview_tr(words),
                                     info ? rubraview_tr(info->heading) : "?")
                          : snprintf(buffer + n, capacity - (size_t)n, "%s%.*s (%s)", more ? "  ·  " : " ", (int)o->action.size,
                                     (const char *)o->action.ptr, info ? rubraview_tr(info->heading) : "?");
            if (m <= 0 || (size_t)m >= capacity - (size_t)n) break;
            n += m;
            more = true;
        }
        return n > 0 ? (size_t)n < capacity ? (size_t)n : capacity - 1 : 0;
    }
    int32_t at = g_key_row[index - 1];
    if (at < 0) {   /* RV-184: a context's heading, with when it is asked */
        size_t count = 0;
        const rubraview_key_context_info_t *all = rubraview_keymap_contexts(&count);
        size_t c = (size_t)(-at - 1);
        int n = c < count ? snprintf(buffer, capacity, "-- %s (%s) --", rubraview_tr(all[c].heading), rubraview_tr(all[c].when))
                          : snprintf(buffer, capacity, "-- %s --", T("other"));
        return n > 0 ? (size_t)n < capacity ? (size_t)n : capacity - 1 : 0;
    }
    const rubraview_key_binding_t *b = &app->keymap.bindings[at];
    char keys[160];
    size_t used = 0;
    keys[0] = '\0';
    if (app->key_capture == (int32_t)index) {
        used = (size_t)snprintf(keys, sizeof(keys), T("> press a key (Esc: leave it)"));
    } else {
        for (size_t c = 0; c < b->combo_count && used + 32 < sizeof(keys); ++c) {
            char one[64];
            proven_u8str_view_t text = rubraview_key_combo_format(one, sizeof(one), b->combos[c]);
            int n = snprintf(keys + used, sizeof(keys) - used, "%s%.*s", c ? ", " : "", (int)text.size, text.ptr);
            if (n > 0) used += (size_t)n;
        }
        /* §3.22.2: a key another action also claims where the two meet —
           only one of them is ever reached. Named, not fixed (D-14). */
        for (size_t i = 0; i < app->keymap.count && used + 40 < sizeof(keys); ++i) {
            const rubraview_key_binding_t *o = &app->keymap.bindings[i];
            if (i == (size_t)at || !rubraview_keymap_contexts_meet(o->context, b->context)) continue;
            bool clash = false;
            for (size_t x = 0; x < b->combo_count && !clash; ++x)
                for (size_t y = 0; y < o->combo_count && !clash; ++y)
                    clash = rubraview_key_combo_equal(b->combos[x], o->combos[y]);
            if (!clash) continue;
            int n = snprintf(keys + used, sizeof(keys) - used, T("  ! also %.*s"), (int)o->action.size, o->action.ptr);
            if (n > 0) used += (size_t)n;
            break;
        }
        if (used >= sizeof(keys)) used = sizeof(keys) - 1;
        if (b->combo_count == 0 && used == 0) used = (size_t)snprintf(keys, sizeof(keys), T("(no key)"));
    }
    int n = snprintf(buffer, capacity, "  %-22.*s %.*s", (int)b->action.size, b->action.ptr, (int)used, keys);
    if (n <= 0) return 0;
    return (size_t)n < capacity ? (size_t)n : capacity - 1;
}

/* RV-184 (D-219): the Keys table's rows — by function, a heading a context (in the order of
   rubraview_keymap_contexts, then any other) with its actions under it; by key, each key once. Built again
   whenever the keymap or the way it is shown changes. */
void keymap_table_refresh(app_state_t *app) {
    g_key_row_count = 0;
    if (g_keys_by_key) {
        for (size_t i = 0; i < app->keymap.count; ++i) {
            for (size_t c = 0; c < app->keymap.bindings[i].combo_count && g_key_row_count < KEY_ROWS_MAX; ++c) {
                rubraview_key_combo_t combo = app->keymap.bindings[i].combos[c];
                bool seen = false;
                for (size_t r = 0; r < g_key_row_count && !seen; ++r)
                    seen = rubraview_key_combo_equal(app->keymap.bindings[g_key_combo[r].binding].combos[g_key_combo[r].combo], combo);
                if (!seen) g_key_combo[g_key_row_count++] = (key_combo_row_t){ .binding = i, .combo = c };
            }
        }
    } else {
        size_t count = 0;
        const rubraview_key_context_info_t *all = rubraview_keymap_contexts(&count);
        for (size_t c = 0; c <= count; ++c) {
            bool heading = false;
            for (size_t i = 0; i < app->keymap.count && g_key_row_count + 1 < KEY_ROWS_MAX; ++i) {
                proven_u8str_view_t ctx = app->keymap.bindings[i].context;
                bool here = c < count ? proven_u8str_view_eq(ctx, proven_u8str_view_from_cstr(all[c].name))
                                      : rubraview_keymap_context_info(ctx) == NULL;
                if (!here) continue;
                if (!heading) { g_key_row[g_key_row_count++] = -(int32_t)c - 1; heading = true; }
                g_key_row[g_key_row_count++] = (int32_t)i;
            }
        }
    }
    rubraview_settings_view_set_table_rows(&app->settings_view, (int32_t)g_key_row_count + 1);   /* heading + rows */
}

/* The binding a Keys row is, or -1 for a heading or a row of the by-key list. */
static int32_t keymap_row_binding(int32_t row) {
    if (g_keys_by_key || row < 1 || (size_t)row > g_key_row_count) return -1;
    return g_key_row[row - 1];
}

static void settings_measure(app_state_t *app) {
    double dpi = rubraview_pal_window_dpi_scale(app->settings_window);
    /* 1.25.0 (owner, 2026-10-20): its own face, size and line spacing (Settings › General) */
    double size = rubraview_settings_get(&app->settings, PROVEN_LIT("general"), PROVEN_LIT("size_settings")) / 100.0;
    double spacing = rubraview_settings_get(&app->settings, PROVEN_LIT("general"), PROVEN_LIT("settings_spacing")) / 100.0;
    if (!(size >= 0.6)) size = 1.0;
    if (!(spacing >= 1.0)) spacing = 1.0;
    rubraview_font_spec_t face = rubraview_font_spec_parse(rubraview_settings_get_text(&app->settings, PROVEN_LIT("general"), PROVEN_LIT("font_settings")));
    rubraview_pal_render_set_mono_family(app->settings_renderer, face.family);
    app->settings_font = 15.0 * dpi * size;
    if (!rubraview_pal_render_mono_cell(app->settings_renderer, app->settings_font,
                                        &app->settings_cell_w, &app->settings_cell_h) ||
        app->settings_cell_w <= 0.0 || app->settings_cell_h <= 0.0) {
        app->settings_cell_w = 8.3 * dpi * size;     /* Consolas at 15 px, if measuring failed */
        app->settings_cell_h = 17.6 * dpi * size;
    }
    double natural = app->settings_cell_h;
    app->settings_cell_h = natural * spacing;
    rubraview_pal_render_set_mono_pad(app->settings_renderer, (app->settings_cell_h - natural) * 0.5);
}

static void settings_grid(const app_state_t *app, int32_t *out_cols, int32_t *out_rows) {
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->settings_window, &w, &h);
    *out_cols = (int32_t)((double)w / app->settings_cell_w);
    *out_rows = (int32_t)(((double)h - app->settings_cell_h * 0.5) / app->settings_cell_h);   /* half a row spare under the buttons */
    if (*out_cols < 40) *out_cols = 40;
    if (*out_rows < 12) *out_rows = 12;
}

void settings_open(app_state_t *app) {
    if (app->settings_open) return;
    /* What changed outside the window goes to disk first, then what is on
       disk now is read — another copy of the program may have written it. */
    settings_write_if_changed(app, false);
    settings_read_file(app);
    settings_took_effect(app);
    app->settings_opened = app->settings;
    app->keymap_saved = rubraview_keymap_copy(app->arena, &app->keymap);
    app->key_capture = 0;

    if (app->settings_window) {
        /* Made on the first F10 and hidden on close: shown again as it was. */
        rubraview_pal_window_set_visible(app->settings_window, true);
        settings_measure(app);
        int32_t cols = 0, rows = 0;
        settings_grid(app, &cols, &rows);
        rubraview_settings_view_resize(&app->settings_view, cols, rows);
        app->settings_open = true;
        app->settings_dirty = true;
        app->settings_mouse_down = false;
        app->settings_message[0] = '\0';
        return;
    }

    rubraview_window_config_t config = {
        .title = T("Rubraview settings"), .width = 980, .height = 680, .owner = app->window,
    };
    app->settings_window = rubraview_pal_window_create(app->arena, &config);
    if (!app->settings_window) {
        osd_say(app, TU8("the settings window could not be opened"));
        return;
    }
    if (app->settings_frame[2] > 0 && app->settings_frame[3] > 0) {
        rubraview_pal_window_set_frame(app->settings_window, app->settings_frame[0], app->settings_frame[1],
                                       app->settings_frame[2], app->settings_frame[3]);
    }
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->settings_window, &w, &h);
    app->settings_renderer = rubraview_pal_render_create(app->arena,
                                                         rubraview_pal_window_native_handle(app->settings_window), w, h);
    ui_font_apply_all(app);
    if (!app->settings_renderer) {
        rubraview_pal_window_destroy(app->settings_window);
        app->settings_window = NULL;
        osd_say(app, TU8("the settings window could not be drawn"));
        return;
    }
    settings_measure(app);
    int32_t cols = 0, rows = 0;
    settings_grid(app, &cols, &rows);
    app->settings_view = rubraview_settings_view_create(rubraview_settings_document(), cols, rows);
    rubraview_settings_view_set_info(&app->settings_view, app, settings_info);   /* 1.30.1: long values wrap */
    keymap_table_refresh(app);
    skip_dirs_table_rows(app);
    app->settings_open = true;
    app->settings_dirty = true;
    app->settings_mouse_down = false;
    app->settings_message[0] = '\0';
}

/* RFC-0006: settings.ini's text as written, and the folders read from it, in
   turns: the idle half emptied and made, then swapped in. Without the halves,
   the session's memory (as it was before). */
static proven_arena_t *settings_half_next(app_state_t *app) {
    /* the file parsed, every key set, and written out again: some times the 256 KB it may be */
    proven_arena_t *a = app->mem ? rubraview_halves_next(&app->mem->settings, 4u << 20) : NULL;
    return a ? a : app->arena;
}

static void settings_half_swap(app_state_t *app, proven_arena_t *half) {
    if (app->mem && half != app->arena) rubraview_halves_swap(&app->mem->settings);
}

/* settings.ini is written when something in it changed: as the settings
   window closes (1a), before it opens again, and as the viewer quits —
   the volume and the boxes' opacity change outside the window (D-15). */
void settings_write_if_changed(app_state_t *app, bool say) {
    if (!rubraview_settings_differs(&app->settings, &app->settings_saved)) return;
    /* D-99: with several viewers open, only what this one changed goes over
       the file as it is now; what another changed meanwhile stays in it. */
    bool locked = rubraview_pal_config_lock(CONFIG_LOCK_MS);
    /* RFC-0006: the file as it is and the merge, for the moment; the text written goes
       into the idle half of `settings` with the curation folders read from it, and
       that half becomes the one in use (the one before is emptied at the next write). */
    rubraview_scratch_t sc;
    proven_arena_t *sa = scratch_begin(app, &sc, 4u << 20);
    proven_arena_t *half = settings_half_next(app);
    proven_u8str_view_t existing = rubraview_pal_fs_read_file(sa, app->settings_path, 256u * 1024u);
    rubraview_settings_t merged = rubraview_settings_merge_changed(sa, &app->settings, &app->settings_saved, existing);
    proven_u8str_view_t text = rubraview_settings_save(half, &merged, existing);
    scratch_end(app, &sc);
    bool written = text.size > 0 && rubraview_pal_fs_write_file_atomic(app->settings_path, text);
    if (locked) rubraview_pal_config_unlock();
    if (written) {
        app->settings_saved = app->settings;
        rubraview_pal_instance_tell_config_changed(false);   /* D-105: the other viewers read it now */
        /* §3.18.3's folders are read from the same file, so they follow
           at once rather than at the next launch. */
        app->curation = rubraview_curation_parse(half, text);
        settings_half_swap(app, half);
        if (say) osd_say(app, TU8("settings saved"));
    } else {
        app->share_failed = true;
        if (say) osd_say(app, TU8("could not write settings.ini"));
    }
}

/* RV-110: the page on screen could not be read: it says so, once, rather
   than staying empty without a word. JPEG XL is read by a WIC codec, when
   one is installed, or by the user's FFmpeg. A film says its own (media_prepare). */
void page_failure_notice(app_state_t *app) {
    int32_t index = current_page_index(app);
    if (index < 0 || (size_t)index >= page_count(app) || !app->pages) return;
    app_page_t *page = &app->pages[index];
    if (!page->failed || page->failure_said) return;
    page->failure_said = true;
    proven_u8str_view_t name = app->source.pages[index].path.size > 0 ? app->source.pages[index].path : app->source.pages[index].name;
    if (is_media_path(name) || is_text_path(name)) return;
    if (rubraview_path_has_ext(name, ".jxl")) {
        /* What is known of why: Windows' codec was tried first and is not
           there or refused; then the FFmpeg beside the program, if any. */
        osd_say(app, rubraview_jxl_failure_text(rubraview_pal_ffmpeg_jxl_state(), rubraview_pal_ffmpeg_refused()));
    } else {
        osd_say(app, TU8("this picture could not be read"));
    }
    app->notice_seconds = MEDIA_NOTICE_SECONDS;
}

/* D-105: every viewer open has the same settings. A change made here —
   in the settings window, by a key, by the wheel on the volume — is in
   settings.ini a moment after the last of a run of changes, and the
   other viewers are told (owner, 2026-10-12: "하나의 루브라뷰의 설정이
   변하면 다른 인스턴스에도 즉각적으로"). */
#define SHARE_SETTLE_SECONDS 0.3

void settings_share(app_state_t *app, double now) {
    if (app->settings.revision_total != app->share_seen_total) {
        app->share_seen_total = app->settings.revision_total;
        app->share_changed_at = now;
        app->share_failed = false;
        return;
    }
    if (app->share_failed || now - app->share_changed_at < SHARE_SETTLE_SECONDS) return;
    if (app->spath_active) return;   /* a folder is being typed */
    settings_write_if_changed(app, false);
}

/* D-105: another viewer wrote settings.ini (or keymap.ini). The file is
   read; what this viewer changed and has not written yet stays on top of
   it; what differs now takes effect. `settings_saved` becomes the file,
   so this viewer does not write the same thing back. */
void settings_follow(app_state_t *app, int what) {
    if (what & 1) {
    bool locked = rubraview_pal_config_lock(CONFIG_LOCK_MS);
    proven_u8str_view_t existing = rubraview_pal_fs_read_file(app->arena, app->settings_path, 256u * 1024u);
    if (locked) rubraview_pal_config_unlock();
    if (existing.size > 0) {
        rubraview_settings_t loaded = rubraview_settings_load(app->arena, existing);
        rubraview_settings_t mine = rubraview_settings_merge_changed(app->arena, &app->settings, &app->settings_saved, existing);
        bool changed = rubraview_settings_differs(&mine, &app->settings);
        if (app->settings_open && changed) {
            /* Revert undoes what was done in this window, not what another viewer did. */
            proven_u8str_view_t opened = rubraview_settings_save(app->arena, &app->settings_opened, existing);
            app->settings_opened = rubraview_settings_merge_changed(app->arena, &loaded, &app->settings_saved, opened);
        }
        /* The counts of changes are this viewer's own: what it redraws by. */
        memcpy(mine.revision, app->settings.revision, sizeof(mine.revision));
        mine.revision_total = app->settings.revision_total + (changed ? 1u : 0u);
        app->settings = mine;
        app->settings_saved = loaded;
        if (changed) {
            app->share_seen_total = app->settings.revision_total;   /* nothing of this is ours to write */
            proven_arena_t *half = settings_half_next(app);
            app->curation = rubraview_curation_parse(half, existing);
            settings_half_swap(app, half);
            settings_took_effect(app);
            app->settings_dirty = true;
        }
    }
    }
    /* The keys: written as a settings window closes. One being edited here is left alone. */
    if ((what & 2) && !app->settings_open) load_keymap(app);
}

/* 1a: the file is written as the window closes, and only when something
   in it changed. */
void settings_close(app_state_t *app) {
    app->spath_active = false;   /* a folder being typed is dropped with the window */
    if (!app->settings_open) return;
    settings_write_if_changed(app, true);
    app->key_capture = 0;
    if (!rubraview_keymap_equal(&app->keymap, &app->keymap_saved)) {
        proven_u8str_view_t text = rubraview_keymap_serialize(app->arena, &app->keymap);
        if (text.size > 0 && rubraview_pal_fs_write_file_atomic(app->keymap_path, text)) {
            app->keymap_saved = rubraview_keymap_copy(app->arena, &app->keymap);
            rubraview_pal_instance_tell_config_changed(true);   /* D-105 */
            osd_say(app, TU8("keys saved to keymap.ini"));
        } else {
            osd_say(app, TU8("could not write keymap.ini"));
        }
    }
    int32_t *f = app->settings_frame;
    if (!rubraview_pal_window_get_frame(app->settings_window, &f[0], &f[1], &f[2], &f[3])) f[2] = f[3] = 0;
    rubraview_pal_window_set_visible(app->settings_window, false);
    app->settings_open = false;
    layout_save(app);
}

/* What a change means for the viewer right now, beyond what it reads
   every frame (the subtitle size and outline). */
/* The settings the viewer did not read until the owner asked for all of
   them (2026-09-30, "1-A"). Each takes effect now where it can; the ones
   that belong to opening something take effect at the next open. Defaults
   (fit, layout, pixel grid) apply at start and when they are changed, and
   leave alone what the reader changed by hand in between. */
/* RV-114: the language. `general.language` names it; `auto` takes Windows'
   display language. A pack is lang\<code>.txt in the settings folder (a
   user's own) or beside the program; none is built in. A pack that is
   missing or cannot be read leaves the viewer in English and says so. */
static rubraview_lang_t g_lang;
static char g_lang_code[24];   /* what is in use: "" for English */
const char *const LANG_PACKS[] = { "ko", "ja", "zh-Hans", "zh-Hant", "fa" };

/* Where Import puts what cannot go beside the program (the Store's package
   and Program Files take no files): `name` in the settings folder. */
static proven_u8str_view_t imported_folder(app_state_t *app, const char *name) {
    proven_u8str_view_t dir = rubraview_path_dirname(app->settings_path);
    return dir.size > 0 ? rubraview_path_join(app->arena, dir, cstr(name)) : PROVEN_LIT("");
}

static void lang_apply(app_state_t *app) {
    static const char *const CHOICES[] = { "auto", "en", "ko", "ja", "zh-Hans", "zh-Hant", "fa" };
    int c = (int)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("general"), PROVEN_LIT("language")));
    const char *want = CHOICES[c >= 0 && c < 7 ? c : 0];
    if (strcmp(want, "auto") == 0) {
        char tag[96];
        want = rubraview_pal_process_ui_language(tag, sizeof(tag)) ? rubraview_lang_pick(tag, LANG_PACKS, 5) : NULL;
    }
    if (want && strcmp(want, "en") == 0) want = NULL;
    if (strcmp(want ? want : "", g_lang_code) == 0) return;

    rubraview_lang_use(NULL);
    rubraview_lang_free(&g_lang);
    g_lang_code[0] = '\0';
    if (want) {
        char name[48];
        snprintf(name, sizeof(name), "lang/%s.txt", want);
        proven_u8str_view_t dirs[2] = { rubraview_path_dirname(app->settings_path), app->config_beside };
        bool loaded = false;
        for (int i = 0; i < 2 && !loaded; i++) {
            if (dirs[i].size == 0) continue;
            rubraview_scratch_t sc;   /* the pack keeps its own copy; the file read is for the moment */
            proven_arena_t *sa = scratch_begin(app, &sc, RUBRAVIEW_LANG_MAX_FILE + (64u << 10));
            proven_u8str_view_t path = rubraview_path_join(sa, dirs[i], cstr(name));
            proven_u8str_view_t text = rubraview_pal_fs_read_file(sa, path, RUBRAVIEW_LANG_MAX_FILE);
            rubraview_lang_stats_t stats;
            if (text.size > 0) loaded = rubraview_lang_load(&g_lang, text, &stats);
            scratch_end(app, &sc);
        }
        if (loaded) {
            rubraview_lang_use(&g_lang);
            snprintf(g_lang_code, sizeof(g_lang_code), "%s", want);
        } else {
            char line[96];
            int n = snprintf(line, sizeof(line), T("no language pack lang\\%s.txt beside rubraview.exe: English it is"), want);
            if (n > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n < sizeof(line) ? (size_t)n : sizeof(line) - 1 });
        }
    }
    /* Everything that holds text made in the old language is made again. */
    menu_rebuild(app);
    if (app->settings_window) {
        int32_t cols = 0, rows = 0;
        settings_grid(app, &cols, &rows);
        if (cols > 0 && rows > 0) rubraview_settings_view_resize(&app->settings_view, cols, rows);
        app->settings_dirty = true;
    }
    update_window_title(app);
    app->needs_relayout = true;
}

void load_keymap(app_state_t *app);
static void settings_apply_rest(app_state_t *app) {
    static bool first = true;
    static double last_fit = -1.0, last_layout = -1.0, last_grid = -1.0, last_color = -1.0,
                  last_privacy = -1.0, last_keymap = -1.0, last_collapse = -1.0, last_details = -1.0;
    rubraview_settings_t *st = &app->settings;
    double dpi = rubraview_pal_window_dpi_scale(app->window);

    /* General */
    lang_apply(app);
    app->frameless = rubraview_settings_get(st, PROVEN_LIT("general"), PROVEN_LIT("frameless")) > 0.5;
    rubraview_pal_window_set_frameless(app->window, app->frameless);
    app->titlebar.trigger_zone = fmax(36.0, rubraview_settings_get(st, PROVEN_LIT("general"), PROVEN_LIT("titlebar_trigger_px"))) * dpi;   /* never less than the bar (owner, 2026-10-14) */
    app->titlebar.hide_delay = rubraview_settings_get(st, PROVEN_LIT("general"), PROVEN_LIT("titlebar_hide_ms")) / 1000.0;

    /* Viewer */
    double fit = rubraview_settings_get(st, PROVEN_LIT("viewer"), PROVEN_LIT("fit_mode"));
    if (fit != last_fit) {
        static const rubraview_fit_mode_t FIT[] = { RUBRAVIEW_FIT_WINDOW, RUBRAVIEW_FIT_WIDTH, RUBRAVIEW_FIT_HEIGHT,
                                                    RUBRAVIEW_FIT_ACTUAL_SIZE, RUBRAVIEW_FIT_SMART, RUBRAVIEW_FIT_STRETCH };
        int f = (int)lround(fit);
        app->fit_mode = FIT[f >= 0 && f < 6 ? f : 0];
        if (!first) reset_view(app);
        last_fit = fit;
    }
    double layout = rubraview_settings_get(st, PROVEN_LIT("viewer"), PROVEN_LIT("layout"));
    if (layout != last_layout) {
        /* The one place the layout is set from: the settings window, the
           toolbox's tile, the menu and the key all write this setting, so
           they cannot disagree (owner, 2026-10-09). The setting's order is
           rubraview_layout_choice_t's, and the way the pages run comes
           with it (owner, 2026-10-11). */
        int l = (int)lround(layout);
        app->layout_opts.mode = rubraview_layout_choice_mode(l);
        app->layout_opts.direction = rubraview_layout_choice_direction(l);
        /* Comic is one page at a time with a wide scan as its two halves;
           every other layout shows a wide scan whole. */
        app->layout_opts.auto_split_wide_spreads = app->layout_opts.mode == RUBRAVIEW_PAGE_LAYOUT_COMIC;
        app->needs_relayout = true;
        app->strip_offset = 0.0;
        app->strip_base_w = 0.0;
        if (!first) reset_view(app);
        last_layout = layout;
    }
    double collapse = rubraview_settings_get(st, PROVEN_LIT("viewer"), PROVEN_LIT("portrait_collapse"));
    if (collapse != last_collapse) {
        app->layout_opts.portrait_collapse_ar = collapse > 0.5 ? 1.0 : 0.0;   /* 0: never collapse */
        app->needs_relayout = true;
        last_collapse = collapse;
    }
    g_gutter = rubraview_settings_get(st, PROVEN_LIT("viewer"), PROVEN_LIT("gutter_px")) * dpi;
    g_zoom_step = 1.0 + rubraview_settings_get(st, PROVEN_LIT("viewer"), PROVEN_LIT("zoom_step_percent")) / 100.0;
    g_whole_zoom = rubraview_settings_get(st, PROVEN_LIT("viewer"), PROVEN_LIT("whole_zoom")) > 0.5;
    {
        static const uint32_t BACK[] = { 0xFF101010u, 0xFF000000u, 0xFF606060u, 0xFFFFFFFFu };
        int back = (int)lround(rubraview_settings_get(st, PROVEN_LIT("viewer"), PROVEN_LIT("background")));
        g_canvas = BACK[back >= 0 && back < 4 ? back : 0];
        static const uint32_t INK[] = { 0xFFFFFFFFu, 0xFFFFE94Du, 0xFF66E0FFu, 0xFF7CF08Au, 0xFFFF9AD0u };
        int ink = (int)lround(rubraview_settings_get(st, PROVEN_LIT("video"), PROVEN_LIT("subtitle_color")));
        g_subtitle_argb = INK[ink >= 0 && ink < 5 ? ink : 0];
        static uint64_t last_face = 0;
        proven_u8str_view_t face = rubraview_settings_get_text(st, PROVEN_LIT("video"), PROVEN_LIT("subtitle_font"));
        uint64_t h = 1469598103934665603ull;
        for (size_t i = 0; i < face.size; ++i) h = (h ^ (uint8_t)face.ptr[i]) * 1099511628211ull;
        if (h != last_face) { g_subtitle_style++; last_face = h; }
    }
    /* RV-176 (D-208): FFmpeg in a process of its own, or inside, before anything asks for it */
    rubraview_pal_ffmpeg_set_isolated(rubraview_settings_get(st, PROVEN_LIT("video"), PROVEN_LIT("ffmpeg_isolated")) > 0.5);
    /* Before anything asks for FFmpeg, and again on a change: while it is
       not loaded, the new folder is tried at the next ask. */
    rubraview_pal_ffmpeg_set_folders(imported_folder(app, "ffmpeg"), rubraview_pal_fs_expand_env(app->arena,
        rubraview_path_typed_folder(rubraview_settings_get_text(st, PROVEN_LIT("video"), PROVEN_LIT("ffmpeg_dir")))));
    double grid = rubraview_settings_get(st, PROVEN_LIT("viewer"), PROVEN_LIT("pixel_grid"));
    if (grid != last_grid) { app->pixel_grid = grid > 0.5; last_grid = grid; }

    /* Files */
    {
        /* RV-106: what a folder lists, changed while one is open: it is listed again, on the same file. */
        static uint64_t last_listing = 0;
        proven_u8str_view_t pattern = rubraview_settings_get_text(st, PROVEN_LIT("files"), PROVEN_LIT("pattern"));
        uint64_t listing = 1469598103934665603ull;
        for (size_t i = 0; i < pattern.size; ++i) listing = (listing ^ (uint8_t)pattern.ptr[i]) * 1099511628211ull;
        static const char *const KEYS[] = { "sort_mode", "sort_ascending", "subfolders", "show", "same_kind" };   /* RV-169 */
        for (size_t i = 0; i < sizeof(KEYS) / sizeof(KEYS[0]); ++i) {
            listing = (listing ^ (uint64_t)lround(rubraview_settings_get(st, PROVEN_LIT("files"), cstr(KEYS[i])) + 1.0)) * 1099511628211ull;
        }
        if (listing != last_listing && !first) folder_relist(app);
        last_listing = listing;
    }
    int cp = (int)lround(rubraview_settings_get(st, PROVEN_LIT("files"), PROVEN_LIT("archive_codepage")));
    app->archive_codepage = cp >= 0 && cp <= (int)RUBRAVIEW_CODEPAGE_WESTERN ? (rubraview_codepage_t)cp : RUBRAVIEW_CODEPAGE_AUTO;

    /* Audio */
    rubraview_pal_audio_set_latency_ms((uint32_t)lround(rubraview_settings_get(st, PROVEN_LIT("audio"), PROVEN_LIT("wasapi_latency_ms"))));

    picker_columns_read(rubraview_settings_get_text(st, PROVEN_LIT("files"), PROVEN_LIT("picker_columns")));   /* 1.25.0 */
    {   /* 1.25.0: the settings window's face, size or line spacing changed: laid out again */
        static uint64_t last_look;
        proven_u8str_view_t f = rubraview_settings_get_text(st, PROVEN_LIT("general"), PROVEN_LIT("font_settings"));
        uint64_t look = (uint64_t)lround(rubraview_settings_get(st, PROVEN_LIT("general"), PROVEN_LIT("size_settings"))) * 1000u +
                        (uint64_t)lround(rubraview_settings_get(st, PROVEN_LIT("general"), PROVEN_LIT("settings_spacing")));
        for (size_t i = 0; i < f.size; ++i) look = (look ^ (unsigned char)f.ptr[i]) * 1099511628211ull;
        if (look != last_look) {
            last_look = look;
            if (!first && app->settings_open && app->settings_window) {
                settings_measure(app);
                int32_t cols = 0, rows = 0;
                settings_grid(app, &cols, &rows);
                rubraview_settings_view_resize(&app->settings_view, cols, rows);
                app->settings_dirty = true;
            }
        }
    }

    /* Text: shown at all (1.25.0) */
    bool text_on = rubraview_settings_get(st, PROVEN_LIT("text"), PROVEN_LIT("viewer")) > 0.5;
    if (text_on != g_text_on) {
        g_text_on = text_on;
        if (!first) settings_say(app, text_on ? T("text files are listed and shown again")
                                              : T("text files are no longer listed or shown; a text file Windows sends here is not shown either"));
    }

    /* Display */
    static const double TILE[] = { 48.0, 64.0, 96.0 };
    int tile = (int)lround(rubraview_settings_get(st, PROVEN_LIT("display"), PROVEN_LIT("tile_base_px")));
    g_tile_base = TILE[tile >= 0 && tile < 3 ? tile : 1];
    g_widget_scale = rubraview_box_widget_size_clamp(rubraview_settings_get(st, PROVEN_LIT("ui"), PROVEN_LIT("widget_size"))) / 100.0;   /* RV-178 */
    double color = rubraview_settings_get(st, PROVEN_LIT("display"), PROVEN_LIT("color_management"));
    if (color != last_color) {
        rubraview_pal_image_set_color_management(color > 0.5);
        if (!first) unload_all_pages(app);   /* decoded again, with or without the profile */
        last_color = color;
    }
    static const uint32_t ACCENT[] = { 0xFFC0304Au, 0xFF3A78D8u, 0xFF2E9E6Au, 0xFFD08A1Eu, 0xFF1E9E9Eu, 0xFF8A4FD0u };
    int accent = (int)lround(rubraview_settings_get(st, PROVEN_LIT("display"), PROVEN_LIT("accent")));
    g_accent = ACCENT[accent >= 0 && accent < 6 ? accent : 0];

    /* Cache */
    app->cache_budget = (size_t)rubraview_settings_get(st, PROVEN_LIT("cache"), PROVEN_LIT("memory_cap_mb")) * 1024u * 1024u;
    app->precache.want_ahead = app->precache.lookahead = (size_t)lround(rubraview_settings_get(st, PROVEN_LIT("cache"), PROVEN_LIT("lookahead")));
    app->precache.want_behind = app->precache.lookbehind = (size_t)lround(rubraview_settings_get(st, PROVEN_LIT("cache"), PROVEN_LIT("lookbehind")));
    double privacy = rubraview_settings_get(st, PROVEN_LIT("cache"), PROVEN_LIT("privacy_clean"));
    if (privacy != last_privacy) { app->export_options.privacy_clean = privacy > 0.5; last_privacy = privacy; }
    double details = rubraview_settings_get(st, PROVEN_LIT("cache"), PROVEN_LIT("keep_details"));   /* 1.17.0 */
    if (details != last_details) { app->export_options.keep_details = (int32_t)lround(details); last_details = details; }

    /* Keys */
    double keymap = rubraview_settings_get(st, PROVEN_LIT("keys"), PROVEN_LIT("use_keymap_file"));
    if (keymap != last_keymap) {
        if (!first) load_keymap(app);
        last_keymap = keymap;
    }
    first = false;
}

/* RV-136 (owner, 2026-10-18): one face for every word the program writes,
   Settings › General › UI font, on every window's renderer. */
void ui_font_apply_all(app_state_t *app) {
    rubraview_font_spec_t face = rubraview_font_spec_parse(rubraview_settings_get_text(&app->settings, PROVEN_LIT("general"), PROVEN_LIT("ui_font")));
    rubraview_renderer_t *all[] = { app->renderer, app->mini_renderer, app->toolbox_renderer, app->settings_renderer, app->info_renderer,
                                    app->help_renderer, app->files_renderer, app->subs_renderer, app->fontpick_renderer,
                                    app->edit_renderer };
    for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); ++i) {
        if (all[i]) rubraview_pal_render_set_ui_font(all[i], face.family, face.weight, face.italic);
    }
}

void settings_took_effect(app_state_t *app) {
    ui_font_apply_all(app);
    {   /* RFC-0007: plug-ins on or off, or another folder: looked for again */
        size_t before = g_plugin_count;
        plugins_from_settings(app);
        if (app->settings_window && g_plugin_count != before) skip_dirs_table_rows(app);
    }
    gmic_from_settings(app);   /* RFC-0009 C */
    app->media_preferred = rubraview_settings_get(&app->settings, PROVEN_LIT("video"), PROVEN_LIT("decoder")) > 0.5
        ? RUBRAVIEW_BACKEND_FFMPEG : RUBRAVIEW_BACKEND_MEDIA_FOUNDATION;
    /* Owner, 2026-09-15: always on top follows the setting, whichever way it was changed. */
    rubraview_pal_window_set_topmost(app->window,
                                     rubraview_settings_get(&app->settings, PROVEN_LIT("general"), PROVEN_LIT("always_on_top")) > 0.5);
    /* D-15: the session volume follows the setting. */
    rubraview_pal_audio_set_volume(rubraview_settings_get(&app->settings, PROVEN_LIT("audio"), PROVEN_LIT("volume")) / 100.0,
                                   rubraview_settings_get(&app->settings, PROVEN_LIT("audio"), PROVEN_LIT("mute")) > 0.5);
    /* D-38: export's resizes on the graphics card, restarted only when the choice changed. */
    static int applied = -1;
    int mode = (int)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("display"), PROVEN_LIT("gpu_resize")));
    if (mode != applied) {
        applied = mode;
        (void)rubraview_pal_gpu_resample_start((rubraview_gpu_resize_mode_t)mode);
    }
    /* Sort by or Ascending changed: the folder on screen is opened again
       in the new order, at the same picture. Not on the first call, which
       is the settings being read at start. */
    static int sorted = -1;
    bool ascending = true;
    int order = (int)folder_sort_mode(app, &ascending) * 2 + (ascending ? 1 : 0);
    if (sorted >= 0 && order != sorted && app->source.kind == RUBRAVIEW_PAGE_SOURCE_FOLDER && page_count(app) > 1) {
        proven_u8str_view_t here = current_file_path(app);
        if (here.size > 0 && !app->media) open_path(app, here);
    }
    sorted = order;

    settings_apply_rest(app);
    audio_chain_update(app);   /* the equaliser, ReplayGain and night mode (owner, 2026-09-30) */
}

/* §3.22.2: a keymap written out to share or keep, or read back in. What
   is imported replaces the keys shown on the Keys page only — Revert still
   goes back to keymap.ini, and keymap.ini is written as the window closes,
   like a key changed by hand. */
static void keys_export_or_import(app_state_t *app, bool writing) {
    const rubraview_file_filter_t FILTERS[] = { { "Keymap (*.ini)", "*.ini" }, { T("All files"), "*.*" } };
    rubraview_file_dialog_opts_t opts = {
        .title = writing ? T("Export keys") : T("Import keys"),
        .filters = FILTERS, .filter_count = 2,
        .parent_window_handle = rubraview_pal_window_native_handle(app->settings_window),
    };
    app->key_capture = 0;
    if (writing) {
        rubraview_file_dialog_result_t picked = rubraview_pal_file_dialog_save(app->arena, &opts);
        if (!picked.accepted || picked.count == 0) return;
        proven_u8str_view_t text = rubraview_keymap_serialize(app->arena, &app->keymap);
        settings_say(app, text.size > 0 && rubraview_pal_fs_write_file(picked.paths[0], text)
                              ? T("keys exported") : T("could not write that file"));
        return;
    }
    rubraview_file_dialog_result_t picked = rubraview_pal_file_dialog_open(app->arena, &opts);
    if (!picked.accepted || picked.count == 0) return;
    proven_u8str_view_t text = rubraview_pal_fs_read_file(app->arena, picked.paths[0], KEYMAP_MAX_BYTES);
    rubraview_keymap_t keymap = text.size > 0 ? rubraview_keymap_parse(app->arena, text) : (rubraview_keymap_t){0};
    if (keymap.count == 0) {
        settings_say(app, T("that file holds no keys; nothing changed"));
        return;
    }
    app->keymap = keymap;
    keymap_table_refresh(app);
    skip_dirs_table_rows(app);
    settings_say(app, T("keys imported (Revert undoes this)"));
}

/* Import (owner, 2026-10-17): the files copied where the program reads
   them, so nothing has to be put anywhere by hand: beside the program when
   that folder can be written (the zip), else into the settings folder's
   `sub` (the Store's package, Program Files). All are copied first under
   another name, and only when every one is there do they take their
   names, so a copy that fails half way leaves nothing behind (RV-122).
   0 nothing copied, 1 beside, 2 the settings folder. */
static bool import_into(app_state_t *app, proven_u8str_view_t place, const proven_u8str_view_t *from, const proven_u8str_view_t *names, size_t count) {
    proven_u8str_view_t to[16], spare[16];
    if (count > 16 || place.size == 0 || !rubraview_pal_fs_make_dirs(place)) return false;
    size_t done = 0;
    for (; done < count; done++) {
        to[done] = rubraview_path_join(app->arena, place, names[done]);
        spare[done] = rubraview_path_with_ext(app->arena, to[done], ".importing");
        (void)rubraview_pal_fs_delete(spare[done]);
        if (!rubraview_pal_fs_copy(from[done], spare[done])) break;
    }
    bool whole = done == count;
    for (size_t i = 0; i < done; i++) {
        if (whole) whole = rubraview_pal_fs_replace(spare[i], to[i]);
        else (void)rubraview_pal_fs_delete(spare[i]);
    }
    return whole;
}

static int import_files(app_state_t *app, const proven_u8str_view_t *from, const proven_u8str_view_t *names, size_t count, const char *sub,
                        bool beside_in_sub) {
    proven_u8str_view_t beside = beside_in_sub ? rubraview_path_join(app->arena, app->config_beside, cstr(sub)) : app->config_beside;
    if (import_into(app, beside, from, names, count)) return 1;
    return import_into(app, imported_folder(app, sub), from, names, count) ? 2 : 0;
}

static void lang_import(app_state_t *app) {
    const rubraview_file_filter_t FILTERS[] = { { T("Language pack (*.txt)"), "*.txt" } };
    rubraview_file_dialog_opts_t opts = {
        .title = T("Import language packs"), .filters = FILTERS, .filter_count = 1, .allow_multi = true,
        .parent_window_handle = rubraview_pal_window_native_handle(app->settings_window),
    };
    rubraview_file_dialog_result_t picked = rubraview_pal_file_dialog_open(app->arena, &opts);
    if (!picked.accepted || picked.count == 0) return;
    proven_u8str_view_t from[16], names[16];
    size_t count = 0;
    for (size_t i = 0; i < picked.count && count < 16; i++) {
        /* Only a pack this build can choose, under its own name (ko.txt), and one that reads. */
        proven_u8str_view_t stem = rubraview_path_stem(picked.paths[i]);
        const char *code = NULL;
        for (size_t k = 0; k < 5 && !code; k++) {
            size_t at = 0, n = strlen(LANG_PACKS[k]);
            while (at < n && at < stem.size && tolower((unsigned char)stem.ptr[at]) == tolower((unsigned char)LANG_PACKS[k][at])) at++;
            if (at == n && stem.size == n) code = LANG_PACKS[k];
        }
        rubraview_scratch_t sc;   /* read only to see that it is a pack */
        proven_arena_t *sa = scratch_begin(app, &sc, RUBRAVIEW_LANG_MAX_FILE + (64u << 10));
        proven_u8str_view_t text = code ? rubraview_pal_fs_read_file(sa, picked.paths[i], RUBRAVIEW_LANG_MAX_FILE) : PROVEN_LIT("");
        rubraview_lang_t probe = {0};
        rubraview_lang_stats_t stats;
        bool reads = text.size > 0 && rubraview_lang_load(&probe, text, &stats);
        rubraview_lang_free(&probe);
        scratch_end(app, &sc);
        if (!reads) continue;
        char name[32];
        snprintf(name, sizeof(name), "%s.txt", code);
        from[count] = picked.paths[i];
        names[count++] = app_keep(app, cstr(name));
    }
    if (count == 0) {
        settings_say(app, T("no language pack among them (ko.txt, ja.txt, zh-Hans.txt, zh-Hant.txt, fa.txt); nothing copied"));
        return;
    }
    int where = import_files(app, from, names, count, "lang", true);
    /* The settings folder's lang\ is read first (lang_apply): an older pack
       of the same name there would hide the one put beside the program. */
    if (where == 1) {
        proven_u8str_view_t own = imported_folder(app, "lang");
        proven_u8str_view_t beside = rubraview_path_join(app->arena, app->config_beside, PROVEN_LIT("lang"));
        for (size_t i = 0; own.size > 0 && !proven_u8str_view_eq(own, beside) && i < count; i++) {
            if (rubraview_pal_fs_exists(rubraview_path_join(app->arena, own, names[i]))) (void)import_into(app, own, &from[i], &names[i], 1);
        }
    }
    snprintf(g_lang_code, sizeof(g_lang_code), "?");   /* read again, the one in use too */
    lang_apply(app);
    settings_say(app, where == 0 ? T("the language packs could not be copied")
                      : count < picked.count ? T("language packs imported; files that are not one were left out")
                      : T("language packs imported"));
    app->settings_dirty = true;
}

void settings_say(app_state_t *app, const char *text) {
    snprintf(app->settings_message, sizeof(app->settings_message), "%s", text);
}

/* Settings › Explorer (D-86): the extensions switched on, for the file
   types or (`menu`) for the right-click menu, as the words `--types=` takes. Each is read by its own name, so the settings check
   can see that every switch on the page is one the program reads. */
static size_t shell_chosen_words(app_state_t *app, bool menu, char *out, size_t cap) {
    const rubraview_settings_t *st = &app->settings;
    /* RV-120: each extension's two boxes, Open with and the right-click menu. */
    const struct { const char *ext; bool types, menu; } CHOICES[] = {
        { "jpg",  rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("ext_jpg")) > 0.5,  rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("menu_jpg")) > 0.5 },
        { "jpeg", rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("ext_jpeg")) > 0.5, rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("menu_jpeg")) > 0.5 },
        { "png",  rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("ext_png")) > 0.5,  rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("menu_png")) > 0.5 },
        { "apng", rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("ext_apng")) > 0.5, rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("menu_apng")) > 0.5 },
        { "gif",  rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("ext_gif")) > 0.5,  rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("menu_gif")) > 0.5 },
        { "bmp",  rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("ext_bmp")) > 0.5,  rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("menu_bmp")) > 0.5 },
        { "tif",  rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("ext_tif")) > 0.5,  rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("menu_tif")) > 0.5 },
        { "tiff", rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("ext_tiff")) > 0.5, rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("menu_tiff")) > 0.5 },
        { "webp", rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("ext_webp")) > 0.5, rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("menu_webp")) > 0.5 },
        { "avif", rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("ext_avif")) > 0.5, rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("menu_avif")) > 0.5 },
        { "jxl",  rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("ext_jxl")) > 0.5,  rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("menu_jxl")) > 0.5 },
        { "ico",  rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("ext_ico")) > 0.5,  rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("menu_ico")) > 0.5 },
        { "cbz",  rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("ext_cbz")) > 0.5,  rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("menu_cbz")) > 0.5 },
        { "cb7",  rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("ext_cb7")) > 0.5,  rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("menu_cb7")) > 0.5 },
        { "cbr",  rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("ext_cbr")) > 0.5,  rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("menu_cbr")) > 0.5 },
        { "mp4",  rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("ext_mp4")) > 0.5,  rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("menu_mp4")) > 0.5 },
        { "mkv",  rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("ext_mkv")) > 0.5,  rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("menu_mkv")) > 0.5 },
        { "webm", rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("ext_webm")) > 0.5, rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("menu_webm")) > 0.5 },
        { "avi",  rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("ext_avi")) > 0.5,  rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("menu_avi")) > 0.5 },
        { "mov",  rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("ext_mov")) > 0.5,  rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("menu_mov")) > 0.5 },
        /* RV-167 */
        { "wmv",  rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("ext_wmv")) > 0.5,  rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("menu_wmv")) > 0.5 },
        { "asf",  rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("ext_asf")) > 0.5,  rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("menu_asf")) > 0.5 },
        { "mpg",  rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("ext_mpg")) > 0.5,  rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("menu_mpg")) > 0.5 },
        { "mpeg", rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("ext_mpeg")) > 0.5, rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("menu_mpeg")) > 0.5 },
        { "mp3",  rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("ext_mp3")) > 0.5,  rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("menu_mp3")) > 0.5 },
        { "flac", rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("ext_flac")) > 0.5, rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("menu_flac")) > 0.5 },
        { "wav",  rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("ext_wav")) > 0.5,  rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("menu_wav")) > 0.5 },
        { "ogg",  rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("ext_ogg")) > 0.5,  rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("menu_ogg")) > 0.5 },
        { "opus", rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("ext_opus")) > 0.5, rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("menu_opus")) > 0.5 },
        { "m4a",  rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("ext_m4a")) > 0.5,  rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("menu_m4a")) > 0.5 },
        /* RV-123: text files */
        { "txt",  rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("ext_txt")) > 0.5,  rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("menu_txt")) > 0.5 },
        { "log",  rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("ext_log")) > 0.5,  rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("menu_log")) > 0.5 },
        { "nfo",  rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("ext_nfo")) > 0.5,  rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("menu_nfo")) > 0.5 },
        { "md",   rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("ext_md")) > 0.5,   rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("menu_md")) > 0.5 },
        { "zip",  rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("ext_zip")) > 0.5,  rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("menu_zip")) > 0.5 },
        { "7z",   rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("ext_7z")) > 0.5,   rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("menu_7z")) > 0.5 },
        { "rar",  rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("ext_rar")) > 0.5,  rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("menu_rar")) > 0.5 },
        { "alz",  rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("ext_alz")) > 0.5,  rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("menu_alz")) > 0.5 },
        { "egg",  rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("ext_egg")) > 0.5,  rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("menu_egg")) > 0.5 },
    };
    size_t pos = 0;
    out[0] = '\0';
    for (size_t i = 0; i < sizeof(CHOICES) / sizeof(CHOICES[0]); ++i) {
        if (!(menu ? CHOICES[i].menu : CHOICES[i].types)) continue;
        if (pos > 0) append_text(out, cap, &pos, ",");
        append_text(out, cap, &pos, CHOICES[i].ext);
    }
    return pos;
}

/* What a registration did, as the line the reader is shown. The same
   numbers are the exit code of the run made as an administrator. */

/* What a registration does with one of its two parts — the file types
   (double click) and the right-click menu. */

/* Makes the registry match the choice. A part applied is written for the
   extensions of `selection` (`;` between) and ours is taken off every
   other extension; a part removed loses everything of ours; a part left
   is not touched. */
int shell_apply(proven_arena_t *arena, proven_u8str_view_t selection, proven_u8str_view_t menu_selection, shell_part_t types, shell_part_t menu,
                       bool all_users) {
    bool ok = true;
    int code = SHELL_DONE;
    if (types != SHELL_PART_LEAVE) {
        /* Ours is taken off everything first, so an extension switched
           off since the last time loses it. */
        if (!rubraview_pal_shell_unregister_for(rubraview_shell_extensions(), all_users)) ok = false;
        proven_u8str_view_t list = types == SHELL_PART_APPLY ? rubraview_shell_selection_of_kinds(arena, selection, RUBRAVIEW_SHELL_ALL | RUBRAVIEW_SHELL_TEXT)
                                                 : PROVEN_LIT("");
        if (list.size > 0 && !rubraview_pal_shell_register_for(list, all_users)) ok = false;
    }
    if (menu == SHELL_PART_APPLY) {
        switch (rubraview_pal_shell_menu_register(menu_selection.size > 0 ? menu_selection : selection, all_users)) {
            case RUBRAVIEW_SHELL_MENU_FAILED: ok = false; break;
            case RUBRAVIEW_SHELL_MENU_CLASSIC_NO_PARTS: code = SHELL_DONE_NO_PARTS; break;
            case RUBRAVIEW_SHELL_MENU_CLASSIC_REFUSED: code = SHELL_DONE_REFUSED; break;
            case RUBRAVIEW_SHELL_MENU_CLASSIC_STORE: code = SHELL_DONE_STORE; break;   /* D-154 */
            default: break;
        }
    } else if (menu == SHELL_PART_REMOVE && !rubraview_pal_shell_menu_unregister(all_users)) {
        ok = false;
    }
    return ok ? code : SHELL_FAILED;
}

int shell_remove(bool all_users) {
    bool ok = rubraview_pal_shell_unregister_for(rubraview_shell_extensions(), all_users);
    if (!rubraview_pal_shell_menu_unregister(all_users)) ok = false;
    return ok ? SHELL_DONE : SHELL_FAILED;
}

/* D-110: every entry of ours in Explorer, an older version's too: the file
   types and the menu as Remove takes them (each extension given back), then
   whatever is still there. The count removed by the sweep, or -1. */
int shell_wipe(bool all_users) {
    (void)shell_remove(all_users);
    return (int)rubraview_pal_shell_menu_wipe(all_users);
}

/* Settings › Explorer › Remove everything: this user's entries here, then
   every user's through this program run again as an administrator. */
static void shell_wipe_action(app_state_t *app) {
    int mine = shell_wipe(false);
    int code = SHELL_FAILED;
    static const char ARGS[] = "--wipe-shell --all-users";
    bool asked = rubraview_pal_shell_run_elevated((proven_u8str_view_t){ .ptr = (const proven_byte_t *)ARGS, .size = sizeof(ARGS) - 1 }, &code);
    if (mine < 0) settings_say(app, T("could not remove this user's entries"));
    else if (!asked) settings_say(app, T("removed for this user; every user's entries need the administrator's permission, which was not given"));
    else if (code != SHELL_DONE) settings_say(app, T("removed for this user; could not remove every user's entries"));
    else settings_say(app, T("every Rubraview entry is out of Explorer's menu; register again for the ones wanted"));
}

/* Settings › Explorer: the extensions switched on, for this user or —
   through this program run again as an administrator — for every user. */
static void shell_types_action(app_state_t *app, proven_u8str_view_t name) {
    bool adding = proven_u8str_view_eq(name, PROVEN_LIT("shell.register")) || proven_u8str_view_eq(name, PROVEN_LIT("shell.register_all"));
    bool all = proven_u8str_view_eq(name, PROVEN_LIT("shell.register_all")) || proven_u8str_view_eq(name, PROVEN_LIT("shell.unregister_all"));
    bool types = rubraview_settings_get(&app->settings, PROVEN_LIT("shell"), PROVEN_LIT("open_with")) > 0.5;
    bool menu = rubraview_settings_get(&app->settings, PROVEN_LIT("shell"), PROVEN_LIT("menu")) > 0.5;

    char words[256], menu_words[256];
    size_t words_len = shell_chosen_words(app, false, words, sizeof(words));
    size_t menu_len = shell_chosen_words(app, true, menu_words, sizeof(menu_words));
    /* A column with no box ticked is that part taken back (RV-120). */
    if (words_len == 0) types = false;
    if (menu_len == 0) menu = false;
    if (adding && !types && !menu) { settings_say(app, T("switch on at least one extension first")); return; }

    int code = SHELL_FAILED;
    if (!all) {
        /* The page's two switches: what is off is taken back. */
        code = adding ? shell_apply(app->arena, rubraview_shell_selection_parse(app->arena, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)words, .size = words_len }),
                                    rubraview_shell_selection_parse(app->arena, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)menu_words, .size = menu_len }),
                                    types ? SHELL_PART_APPLY : SHELL_PART_REMOVE, menu ? SHELL_PART_APPLY : SHELL_PART_REMOVE, false)
                      : shell_remove(false);
    } else {
        char args[768];
        int n = adding ? snprintf(args, sizeof(args), "--register-shell --all-users --types=%s%s%s%s%s",
                                  words_len > 0 ? words : menu_words, menu_len > 0 ? " --menu-types=" : "", menu_len > 0 ? menu_words : "",
                                  types ? "" : " --remove-types", menu ? "" : " --remove-menu")
                       : snprintf(args, sizeof(args), "--unregister-shell --all-users");
        if (n <= 0 || (size_t)n >= sizeof(args) ||
            !rubraview_pal_shell_run_elevated((proven_u8str_view_t){ .ptr = (const proven_byte_t *)args, .size = (size_t)n }, &code)) {
            settings_say(app, T("not done: the administrator's permission was not given"));
            return;
        }
    }

    const char *who = all ? T("for every user") : T("for this user");
    char say[200];
    app->other_copies = rubraview_pal_shell_other_copies();
    if (code != SHELL_DONE && code != SHELL_DONE_NO_PARTS && code != SHELL_DONE_REFUSED && code != SHELL_DONE_STORE) {
        snprintf(say, sizeof(say), T("could not %s %s"), adding ? T("register") : T("remove"), who);
    } else if (!adding) {
        snprintf(say, sizeof(say), T("removed %s"), who);
    } else if (!menu) {
        snprintf(say, sizeof(say), T("registered %s: file types"), who);
    } else if (!all) {
        snprintf(say, sizeof(say), T("registered %s; the menu is under \"Show more options\" (Windows 11's own menu: for every user)"), who);
    } else if (code == SHELL_DONE_NO_PARTS) {
        snprintf(say, sizeof(say), T("registered %s; Windows 11's menu needs rubraview_menu.dll and .msix beside the program (they are in the zip)"), who);
    } else if (code == SHELL_DONE_REFUSED) {
        snprintf(say, sizeof(say), T("registered %s; Windows did not take the Windows 11 menu's package"), who);
    } else if (code == SHELL_DONE_STORE) {
        snprintf(say, sizeof(say), T("registered %s; Windows 11's menu is left to the Store's copy (this one's is under \"Show more options\")"), who);
    } else {
        snprintf(say, sizeof(say), T("registered %s, with Windows 11's menu"), who);
    }
    settings_say(app, say);
}

static void settings_event(app_state_t *app, rubraview_settings_event_t event) {
    {
        /* The setting the focus was last on: "this setting" is pressed with the focus on the button. */
        const rubraview_settings_node_t *node = rubraview_settings_view_focused_node(&app->settings_view);
        if (node && node->kind == RUBRAVIEW_NODE_SETTING) app->settings_last_setting = node->setting;
    }
    switch (event) {
        case RUBRAVIEW_SEVENT_NONE:
            return;
        case RUBRAVIEW_SEVENT_MOVED:
            break;
        case RUBRAVIEW_SEVENT_CHOOSE:   /* 1.24.0: a choice's words, a line each */
            chooser_open_setting(app);
            break;
        case RUBRAVIEW_SEVENT_TYPE_NUMBER:   /* 1.24.0: a slider's number typed */
            settings_number_begin(app);
            break;
        case RUBRAVIEW_SEVENT_CHANGED:
            settings_took_effect(app);
            app->settings_message[0] = '\0';
            break;
        case RUBRAVIEW_SEVENT_REVERT: {
            uint32_t total = app->settings.revision_total;
            app->settings = app->settings_opened;
            app->settings.revision_total = total + 1;
            app->keymap = rubraview_keymap_copy(app->arena, &app->keymap_saved);
            keymap_table_refresh(app);
            skip_dirs_table_rows(app);
            app->key_capture = 0;
            settings_took_effect(app);
            settings_say(app, T("back to what the settings were when this window opened"));
            break;
        }
        case RUBRAVIEW_SEVENT_DEFAULTS:
            rubraview_settings_reset(&app->settings);
            app->keymap = rubraview_keymap_parse(app->arena, cstr(rubraview_default_keymap()));
            keymap_table_refresh(app);
            skip_dirs_table_rows(app);
            app->key_capture = 0;
            settings_took_effect(app);
            settings_say(app, T("every setting and key is at its default (Revert undoes this)"));
            break;
        case RUBRAVIEW_SEVENT_DEFAULTS_PAGE: {
            /* The owner (2026-10-17): this page only; on Keys, the keys. */
            rubraview_settings_reset_page(&app->settings, (rubraview_settings_tab_t)app->settings_view.page);
            if (app->settings_view.page == RUBRAVIEW_TAB_KEYS) {
                app->keymap = rubraview_keymap_parse(app->arena, cstr(rubraview_default_keymap()));
                keymap_table_refresh(app);
                app->key_capture = 0;
            }
            skip_dirs_table_rows(app);
            settings_took_effect(app);
            settings_say(app, T("this page is at its defaults (Revert undoes this)"));
            break;
        }
        case RUBRAVIEW_SEVENT_DEFAULTS_ONE: {
            if (app->settings_last_setting < 0) { settings_say(app, T("put the focus on a setting first (the arrows, or a click on it)")); break; }
            const rubraview_setting_def_t *def = &rubraview_settings_document()->defs[app->settings_last_setting];
            rubraview_settings_reset_one(&app->settings, def->section, def->key);
            settings_took_effect(app);
            settings_say(app, T("that setting is at its default (Revert undoes this)"));
            break;
        }
        case RUBRAVIEW_SEVENT_CLOSE:
            settings_close(app);
            return;
        case RUBRAVIEW_SEVENT_TABLE_EDIT:
        case RUBRAVIEW_SEVENT_TABLE_CLEAR: {
            int32_t row = rubraview_settings_view_focused_table_row(&app->settings_view);
            int32_t folders = folder_list_of_table(rubraview_settings_view_focused_table(&app->settings_view));
            if (folders >= 0) {
                /* A folder of the list: Enter types or browses it (the last row adds one), Delete takes it off. */
                size_t n = folder_list_count(app, folders);
                if (row < 1) break;
                if (event == RUBRAVIEW_SEVENT_TABLE_CLEAR) {
                    if ((size_t)row > n) break;
                    char buffer[8192];
                    proven_u8str_view_t list = rubraview_folder_list_remove(buffer, sizeof(buffer), folder_list_text(app, folders), (size_t)row - 1);
                    folder_list_set(app, folders, app_keep(app, list));
                    skip_dirs_table_rows(app);
                    settings_took_effect(app);
                    settings_say(app, folders == FOLDER_LIST_THUMBS ? T("folder taken off: its thumbnails are shown again")
                                                                    : T("folder taken off: its files are written down again from now on"));
                } else {
                    const rubraview_setting_def_t *def = rubraview_settings_find(cstr(FOLDER_LISTS[folders].section), cstr(FOLDER_LISTS[folders].key));
                    if (def) settings_path_begin(app, def);
                    app->spath_list = folders;
                    app->spath_list_index = row - 1;   /* the folder in place, or past the end: a new one */
                    proven_u8str_view_t dirs[256];
                    size_t have = rubraview_folder_list_split(folder_list_text(app, folders), dirs, 256);
                    rubraview_textedit_set(&app->spath_edit, (size_t)(row - 1) < have ? dirs[row - 1] : PROVEN_LIT(""));
                    rubraview_textedit_select_all(&app->spath_edit);
                    /* Typed in the box that is always there, the list's last row, never over another row. */
                    rubraview_settings_view_focus_table_row(&app->settings_view, cstr(FOLDER_LISTS[folders].table), (int32_t)n + 1);
                    if ((size_t)row <= n) settings_say(app, T("change the folder in the box; Enter keeps it, Esc leaves it, Ctrl+O browses"));
                }
                break;
            }
            if (g_keys_by_key) { settings_say(app, T("the keys by key are to read: Show by function to change them")); break; }
            int32_t at = keymap_row_binding(row);
            if (at < 0) break;   /* a context's heading */
            const rubraview_key_binding_t *b = &app->keymap.bindings[at];
            char text[160];
            if (event == RUBRAVIEW_SEVENT_TABLE_EDIT) {
                app->key_capture = row;
                snprintf(text, sizeof(text), T("press the key to add to %.*s (Esc: leave it as it is)"),
                         (int)b->action.size, b->action.ptr);
            } else if (b->combo_count == 0) {
                snprintf(text, sizeof(text), T("%.*s has no key to take off"), (int)b->action.size, b->action.ptr);
            } else {
                char one[64];
                proven_u8str_view_t last = rubraview_key_combo_format(one, sizeof(one), b->combos[b->combo_count - 1]);
                rubraview_keymap_unbind_last(&app->keymap, (size_t)at);
                snprintf(text, sizeof(text), T("took %.*s off %.*s (Revert puts it back)"),
                         (int)last.size, last.ptr, (int)b->action.size, b->action.ptr);
            }
            settings_say(app, text);
            break;
        }
        case RUBRAVIEW_SEVENT_EDIT_TEXT:
        case RUBRAVIEW_SEVENT_CLEAR_TEXT: {
            const rubraview_settings_node_t *node = rubraview_settings_view_focused_node(&app->settings_view);
            if (!node || node->kind != RUBRAVIEW_NODE_SETTING) break;
            const rubraview_setting_def_t *def = &app->settings_view.doc->defs[node->setting];
            if (event == RUBRAVIEW_SEVENT_CLEAR_TEXT) {
                rubraview_settings_set_text(&app->settings, def->section, def->key, PROVEN_LIT(""));
                settings_say(app, "cleared");
                break;
            }
            /* RV-136 (owner, 2026-10-18): a font is chosen in the program's own picker. */
            if ((proven_u8str_view_eq(def->section, PROVEN_LIT("video")) && proven_u8str_view_eq(def->key, PROVEN_LIT("subtitle_font"))) ||
                (proven_u8str_view_eq(def->section, PROVEN_LIT("text")) && proven_u8str_view_eq(def->key, PROVEN_LIT("font"))) ||
                (proven_u8str_view_eq(def->section, PROVEN_LIT("general")) && proven_u8str_view_eq(def->key, PROVEN_LIT("ui_font")))) {
                fontpick_show(app, def->section, def->key);
                break;
            }
            settings_path_begin(app, def);   /* typed, pasted, copied, or browsed with Ctrl+O */
            break;
        }
        case RUBRAVIEW_SEVENT_ACTION: {
            const rubraview_settings_node_t *node = rubraview_settings_view_focused_node(&app->settings_view);
            if (!node) break;
            if (app->packaged && (proven_u8str_view_eq(node->name, PROVEN_LIT("shell.register")) || proven_u8str_view_eq(node->name, PROVEN_LIT("shell.unregister")) ||
                                  proven_u8str_view_eq(node->name, PROVEN_LIT("shell.register_all")) || proven_u8str_view_eq(node->name, PROVEN_LIT("shell.unregister_all")) ||
                                  proven_u8str_view_eq(node->name, PROVEN_LIT("shell.wipe")))) {
                /* RV-113 (2): what a packaged program registers goes to the
                   package's own registry, which Explorer does not read; the
                   package's manifest gave Windows its file types already. */
                settings_say(app, T("the Store's copy: Windows has its file types from the package; choose Rubraview in Default apps"));
            } else if (proven_u8str_view_eq(node->name, PROVEN_LIT("shell.register")) || proven_u8str_view_eq(node->name, PROVEN_LIT("shell.unregister")) ||
                proven_u8str_view_eq(node->name, PROVEN_LIT("shell.register_all")) || proven_u8str_view_eq(node->name, PROVEN_LIT("shell.unregister_all"))) {
                shell_types_action(app, node->name);
            } else if (proven_u8str_view_eq(node->name, PROVEN_LIT("shell.wipe"))) {
                shell_wipe_action(app);
            } else if (proven_u8str_view_eq(node->name, PROVEN_LIT("shell.defaults"))) {
                rubraview_pal_shell_open_default_apps();
            } else if (proven_u8str_view_eq(node->name, PROVEN_LIT("ffmpeg.download"))) {
                /* The owner (2026-10-17): where to get FFmpeg, one press away. */
                settings_say(app, rubraview_pal_shell_open_web("https://github.com/BtbN/FFmpeg-Builds/releases")
                                      ? T("the FFmpeg download page is opening in the browser")
                                      : T("the browser could not be started"));
            } else if (proven_u8str_view_eq(node->name, PROVEN_LIT("gmic.download"))) {
                /* RFC-0009 C: G'MIC's makers' page; nothing is fetched by us */
                settings_say(app, rubraview_pal_shell_open_web("https://gmic.eu/download.html")
                                      ? T("the G'MIC download page is opening in the browser")
                                      : T("the browser could not be started"));
            } else if (proven_u8str_view_eq(node->name, PROVEN_LIT("keys.by_key"))) {   /* RV-184 */
                g_keys_by_key = !g_keys_by_key;
                app->key_capture = 0;
                keymap_table_refresh(app);
                settings_say(app, g_keys_by_key ? T("the keys by key: what each does, and where") : T("the keys by function, a context at a time"));
            } else if (proven_u8str_view_eq(node->name, PROVEN_LIT("app.open_folder"))) {   /* RV-185 */
                settings_say(app, rubraview_pal_shell_open_folder(program_folder(app), false)
                                      ? T("Rubraview's folder is opening") : T("the folder could not be opened"));
            } else if (proven_u8str_view_eq(node->name, PROVEN_LIT("app.copy_folder"))) {
                settings_say(app, rubraview_pal_clipboard_set_text(app->settings_window, program_folder(app))
                                      ? T("Rubraview's folder was copied") : T("the clipboard could not be written"));
            } else if (proven_u8str_view_eq(node->name, PROVEN_LIT("ffmpeg.open_folder"))) {
                proven_u8str_view_t chosen = rubraview_pal_fs_expand_env(app->arena,
                    rubraview_path_typed_folder(rubraview_settings_get_text(&app->settings, PROVEN_LIT("video"), PROVEN_LIT("ffmpeg_dir"))));
                int place = rubraview_pal_ffmpeg_place();
                /* the one in use; none yet: the one named, else beside the program, where it is looked for first */
                proven_u8str_view_t dir = place == 2 ? imported_folder(app, "ffmpeg")
                                        : place == 3 || (place == 0 && chosen.size > 0) ? chosen : program_folder(app);
                settings_say(app, rubraview_pal_shell_open_folder(dir, false)
                                      ? T("FFmpeg's folder is opening") : T("the folder named is not there"));
            } else if (proven_u8str_view_eq(node->name, PROVEN_LIT("gmic.open_folder"))) {
                gmic_from_settings(app);
                proven_u8str_view_t at = g_gmic_exe[0] ? cstr(g_gmic_exe) : gmic_named_folder(app);
                settings_say(app, at.size == 0 ? T("not set: name gmic.exe or its folder above")
                                  : rubraview_pal_shell_open_folder(at, false) ? T("G'MIC's folder is opening")
                                  : T("the folder named is not there"));
            } else if (proven_u8str_view_eq(node->name, PROVEN_LIT("plugins.open_folder"))) {
                /* the folder plug-ins are looked for in, the default one when none is named; made when missing,
                   as it is where the files are to be put */
                proven_u8str_view_t named = rubraview_settings_get_text(&app->settings, PROVEN_LIT("edit"), PROVEN_LIT("plugin_folder"));
                char folder[520] = "";
                if (named.size > 0 && named.size < sizeof(folder)) { memcpy(folder, named.ptr, named.size); folder[named.size] = '\0'; }
                else (void)rubraview_pal_plugins_default_folder(folder, sizeof(folder), true);
                settings_say(app, folder[0] && rubraview_pal_shell_open_folder(cstr(folder), true)
                                      ? T("the plug-in folder is opening") : T("the folder could not be opened"));
            } else if (proven_u8str_view_eq(node->name, PROVEN_LIT("gmic.show_list"))) {   /* RV-186 */
                gmic_list_show(app);
            } else if (proven_u8str_view_eq(node->name, PROVEN_LIT("gmic.list"))) {
                gmic_list_make(app);
            } else if (proven_u8str_view_eq(node->name, PROVEN_LIT("app.restart"))) {
                restart_now(app);
            } else if (proven_u8str_view_eq(node->name, PROVEN_LIT("cache.clear_thumbs"))) {
                /* RV-107: the picker's pictures are kept in memory only; they are made again as tiles come into view. */
                picker_thumbs_free(app);
                app->picker_generation++;
                if (app->thumbs) rubraview_pal_thumbs_generation(app->thumbs, app->picker_generation);
                if (app->picker_open) {
                    app->picker_thumbs = (struct picker_thumb *)calloc(app->picker_listing.count ? app->picker_listing.count : 1,
                                                                       sizeof(*app->picker_thumbs));
                    app->picker_thumb_count = app->picker_thumbs ? app->picker_listing.count : 0;
                }
                settings_say(app, T("the picker's thumbnails were let go"));
            } else if (proven_u8str_view_eq(node->name, PROVEN_LIT("cache.reset_history"))) {
                /* Every place read to, here and in the file. Another viewer that is open still has its own. */
                app->history = (rubraview_history_t){0};
                app->resume_offer = false;
                bool locked = rubraview_pal_config_lock(CONFIG_LOCK_MS);
                bool written = app->history_path.size > 0 &&
                               rubraview_pal_fs_write_file_atomic(app->history_path, rubraview_history_serialize(app->arena, &app->history));
                if (locked) rubraview_pal_config_unlock();
                settings_say(app, written ? T("every reading place is forgotten") : T("forgotten here; the history file could not be written"));
            } else if (proven_u8str_view_eq(node->name, PROVEN_LIT("keys.export")) || proven_u8str_view_eq(node->name, PROVEN_LIT("keys.import"))) {
                keys_export_or_import(app, proven_u8str_view_eq(node->name, PROVEN_LIT("keys.export")));
            } else if (proven_u8str_view_eq(node->name, PROVEN_LIT("lang.import"))) {
                lang_import(app);
            }
            break;
        }
    }
    app->settings_dirty = true;
}

static bool settings_key_of(rubraview_key_combo_t combo, rubraview_settings_key_t *out) {
    static const struct { const char *name; rubraview_settings_key_t key; } KEYS[] = {
        { "Up", RUBRAVIEW_SKEY_UP }, { "Down", RUBRAVIEW_SKEY_DOWN },
        { "Left", RUBRAVIEW_SKEY_LEFT }, { "Right", RUBRAVIEW_SKEY_RIGHT },
        { "PageUp", RUBRAVIEW_SKEY_PAGE_UP }, { "PageDown", RUBRAVIEW_SKEY_PAGE_DOWN },
        { "Space", RUBRAVIEW_SKEY_SPACE }, { "Enter", RUBRAVIEW_SKEY_ENTER },
        { "Home", RUBRAVIEW_SKEY_HOME }, { "End", RUBRAVIEW_SKEY_END },
        { "Escape", RUBRAVIEW_SKEY_ESCAPE }, { "Delete", RUBRAVIEW_SKEY_DELETE },
        { "Backspace", RUBRAVIEW_SKEY_DELETE },
    };
    if (key_is(combo, "Tab")) {
        *out = (combo.modifiers & RUBRAVIEW_MOD_SHIFT) ? RUBRAVIEW_SKEY_SHIFT_TAB : RUBRAVIEW_SKEY_TAB;
        return true;
    }
    for (size_t i = 0; i < sizeof(KEYS) / sizeof(KEYS[0]); ++i) {
        if (key_is(combo, KEYS[i].name)) { *out = KEYS[i].key; return true; }
    }
    return false;
}

/* The key pressed for a Keys row (D-14): added, or refused with the name
   of the action that has it — never taken away from it (owner, 2026-09-14). */
static void settings_capture_key(app_state_t *app, rubraview_key_combo_t combo) {
    int32_t at = keymap_row_binding(app->key_capture);
    app->key_capture = 0;
    if (at < 0) return;
    size_t index = (size_t)at;
    if (combo.modifiers == 0 && proven_u8str_view_eq(combo.key_name, PROVEN_LIT("Escape"))) {
        settings_say(app, T("no key added"));
        return;
    }
    char one[64], text[200];
    proven_u8str_view_t name = rubraview_key_combo_format(one, sizeof(one), combo);
    const rubraview_key_binding_t *holder = NULL;
    rubraview_bind_result_t result = rubraview_keymap_bind(app->arena, &app->keymap, index, combo, &holder);
    const rubraview_key_binding_t *b = &app->keymap.bindings[index];
    switch (result) {
        case RUBRAVIEW_BIND_ADDED:
            snprintf(text, sizeof(text), T("%.*s added to %.*s"), (int)name.size, name.ptr, (int)b->action.size, b->action.ptr);
            break;
        case RUBRAVIEW_BIND_ALREADY:
            snprintf(text, sizeof(text), T("%.*s already has %.*s"), (int)b->action.size, b->action.ptr, (int)name.size, name.ptr);
            break;
        case RUBRAVIEW_BIND_TAKEN:
            snprintf(text, sizeof(text), T("not changed: %.*s is %.*s's key%s%.*s%s"), (int)name.size, name.ptr,
                     (int)holder->action.size, holder->action.ptr,
                     holder->context.size ? " (" : "", (int)holder->context.size, holder->context.ptr,
                     holder->context.size ? ")" : "");
            break;
        case RUBRAVIEW_BIND_FAILED:
        default:
            snprintf(text, sizeof(text), T("could not add %.*s"), (int)name.size, name.ptr);
            break;
    }
    settings_say(app, text);
}

/* Everything the settings window's queue holds; returns how many. */
/* ---- A folder typed or pasted in the settings window (owner, 2026-09-30:
   "경로 직접 입력 가능하게. 반대로 현재 경로를 편집 가능한 에디트 컨트롤에
   넣어서 복사해갈 수도 있게") ---- */

void settings_path_begin(app_state_t *app, const rubraview_setting_def_t *def) {
    app->spath_edit = rubraview_textedit_make(app->spath_buffer, sizeof(app->spath_buffer));
    proven_u8str_view_t now = rubraview_settings_get_text(&app->settings, def->section, def->key);
    rubraview_textedit_set(&app->spath_edit, now.size < sizeof(app->spath_buffer) ? now : PROVEN_LIT(""));
    rubraview_textedit_select_all(&app->spath_edit);
    app->spath_section = def->section;
    app->spath_key = def->key;
    app->spath_list_index = -1;
    app->spath_composing_length = 0;
    app->spath_active = true;
    rubraview_pal_window_text_input(app->settings_window, true);
    settings_say(app, T("type or paste a folder; Enter keeps it, Esc leaves it, Ctrl+O browses, Ctrl+C copies"));
}

/* 1.24.0 (owner, 2026-10-19: "모든 슬라이드 컨트롤은 텍스트로 직접 입력"): a slider's number typed in the same box. */
void settings_number_begin(app_state_t *app) {
    const rubraview_setting_def_t *def = rubraview_settings_view_focused_def(&app->settings_view);
    if (!def || (def->type != RUBRAVIEW_SETTING_INT && def->type != RUBRAVIEW_SETTING_FLOAT)) return;
    char now[32];
    double v = rubraview_settings_get(&app->settings, def->section, def->key);
    int n = def->type == RUBRAVIEW_SETTING_FLOAT ? snprintf(now, sizeof(now), "%.2f", v) : snprintf(now, sizeof(now), "%.0f", v);
    app->spath_edit = rubraview_textedit_make(app->spath_buffer, sizeof(app->spath_buffer));
    rubraview_textedit_set(&app->spath_edit, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)now, .size = n > 0 ? (size_t)n : 0 });
    rubraview_textedit_select_all(&app->spath_edit);
    app->spath_section = def->section;
    app->spath_key = def->key;
    app->spath_list_index = -1;
    app->spath_composing_length = 0;
    app->spath_number = true;
    app->spath_active = true;
    rubraview_pal_window_text_input(app->settings_window, true);
    char line[160];
    snprintf(line, sizeof(line), T("type a number, %g to %g; Enter keeps it, Esc leaves it"), def->min_value, def->max_value);
    settings_say(app, line);
}

static void settings_path_end(app_state_t *app) {
    app->spath_number = false;
    app->spath_btn_n = 0;
    app->spath_active = false;
    app->spath_list_index = -1;
    app->spath_composing_length = 0;
    rubraview_pal_window_text_input(app->settings_window, false);
    app->settings_dirty = true;
}

static void settings_path_commit(app_state_t *app) {
    proven_u8str_view_t text = rubraview_textedit_text(&app->spath_edit);
    if (app->spath_number) {   /* 1.24.0 */
        char buf[48];
        size_t n = text.size < sizeof(buf) - 1 ? text.size : sizeof(buf) - 1;
        memcpy(buf, text.ptr, n);
        buf[n] = '\0';
        for (size_t i = 0; i < n; ++i) if (buf[i] == ',') buf[i] = '.';
        char *end = NULL;
        double v = strtod(buf, &end);
        if (!end || end == buf) { settings_say(app, T("that is not a number - put it right, or Esc")); return; }
        rubraview_settings_set(&app->settings, app->spath_section, app->spath_key, v);   /* kept inside its range */
        settings_path_end(app);
        settings_event(app, RUBRAVIEW_SEVENT_CHANGED);
        return;
    }
    /* Explorer's "Copy as path" puts quotes round it; spaces at the ends are no part of a folder. */
    while (text.size > 0 && (text.ptr[0] == ' ' || text.ptr[0] == '"')) { text.ptr++; text.size--; }
    while (text.size > 0 && (text.ptr[text.size - 1] == ' ' || text.ptr[text.size - 1] == '"')) text.size--;
    /* RV-106: "Only names like" is typed like a folder and is not one. */
    bool is_folder = !proven_u8str_view_eq(app->spath_section, PROVEN_LIT("text")) &&   /* 1.25.0: an editor, colours */
                     !(proven_u8str_view_eq(app->spath_section, PROVEN_LIT("files")) && proven_u8str_view_eq(app->spath_key, PROVEN_LIT("pattern"))) &&
                     (!proven_u8str_view_eq(app->spath_section, PROVEN_LIT("video")) ||   /* RV-107: a font's name, a language */
                      proven_u8str_view_eq(app->spath_key, PROVEN_LIT("ffmpeg_dir")));
    if (app->spath_list_index >= 0) {
        /* A folder of the list kept out of Recent: put in its place, or added; empty takes it off. */
        rubraview_fs_entry_t is;
        if (text.size > 0 && (!rubraview_pal_fs_stat(app->arena, text, &is) || !is.is_directory)) {
            settings_say(app, T("no such folder - put it right, or Esc"));
            return;
        }
        int32_t folders = app->spath_list;
        proven_u8str_view_t now = folder_list_text(app, folders);
        char buffer[8192];
        proven_u8str_view_t list = text.size == 0 ? rubraview_folder_list_remove(buffer, sizeof(buffer), now, (size_t)app->spath_list_index)
                                     : rubraview_folder_list_put(buffer, sizeof(buffer), now, text, (size_t)app->spath_list_index);
        if (text.size > 0 && list.size == 0) { settings_say(app, T("the list is full")); return; }
        folder_list_set(app, folders, app_keep(app, list));
        app->spath_list_index = -1;
        settings_path_end(app);
        skip_dirs_table_rows(app);
        rubraview_settings_view_focus_table_row(&app->settings_view, cstr(FOLDER_LISTS[folders].table),
                                                (int32_t)folder_list_count(app, folders) + 1);   /* the box, for the next one */
        settings_took_effect(app);
        settings_say(app, text.size == 0 ? T("folder taken off")
                          : folders == FOLDER_LIST_THUMBS ? T("no thumbnails for this folder, and all under it")
                          : T("folder kept out of Recent, and all under it"));
        return;
    }
    if (text.size == 0) {
        rubraview_settings_set_text(&app->settings, app->spath_section, app->spath_key, PROVEN_LIT(""));
        settings_path_end(app);
        settings_took_effect(app);
        settings_say(app, is_folder ? T("folder cleared") : "cleared");
        return;
    }
    rubraview_fs_entry_t found;
    if (!is_folder) {
        rubraview_settings_set_text(&app->settings, app->spath_section, app->spath_key, app_keep(app, text));
        settings_path_end(app);
        settings_took_effect(app);
        settings_say(app, proven_u8str_view_eq(app->spath_section, PROVEN_LIT("files"))
                              ? "kept (Delete clears it); patterns go with ; between them, as *.png;IMG_*"
                              : T("kept (Delete clears it)"));
        return;
    }
    /* %LOCALAPPDATA%\ffmpeg is a folder too: looked for as Windows will read it. */
    if (!rubraview_pal_fs_stat(app->arena, rubraview_pal_fs_expand_env(app->arena, text), &found) || !found.is_directory) {
        settings_say(app, T("no such folder - put it right, or Esc"));
        return;
    }
    /* The settings borrow their text: a copy, not the box's own buffer,
       which the next folder typed would write over (VM, 2026-10-01: Folder
       1 turned into Folder 2's path once Folder 2 was typed). */
    rubraview_settings_set_text(&app->settings, app->spath_section, app->spath_key, app_keep(app, text));
    settings_path_end(app);
    settings_took_effect(app);
    settings_say(app, T("folder kept (Delete clears it)"));
}

static void settings_path_browse(app_state_t *app) {
    rubraview_file_dialog_opts_t opts = {
        .title = T("Choose a folder"), .folder_mode = true,
        .parent_window_handle = rubraview_pal_window_native_handle(app->settings_window),
    };
    rubraview_file_dialog_result_t picked = rubraview_pal_file_dialog_pick_folder(app->arena, &opts);
    if (picked.accepted && picked.count > 0 && picked.paths[0].size < sizeof(app->spath_buffer)) {
        rubraview_textedit_set(&app->spath_edit, picked.paths[0]);   /* in the box, for Enter to keep */
    }
}

/* Keys while the box is open: true when it took the key. */
static bool settings_path_key(app_state_t *app, rubraview_key_combo_t combo) {
    if (!app->spath_active) return false;
    bool ctrl = (combo.modifiers & RUBRAVIEW_MOD_CTRL) != 0;
    if (key_is(combo, "Enter")) {
        textbox_insert(&app->spath_edit, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)app->spath_composing, .size = app->spath_composing_length });
        app->spath_composing_length = 0;
        settings_path_commit(app);
    } else if (key_is(combo, "Escape")) {
        settings_path_end(app);
        settings_say(app, T("folder left as it was"));
    } else if (ctrl && key_is(combo, "O")) {
        settings_path_browse(app);
    } else {
        (void)textbox_key(app->settings_window, &app->spath_edit, combo);
    }
    app->settings_dirty = true;
    return true;   /* nothing else happens while typing */
}

/* Where the typing box is: the list's last row for a folder kept out of
   Recent (drawn there all the time), else over the focused path setting's
   value. Grid row, first column and width; false when out of sight. */
static bool settings_path_box(const app_state_t *app, int32_t list, int32_t *row, int32_t *col0, int32_t *cols) {
    const rubraview_settings_view_t *v = &app->settings_view;
    if (list >= 0) {
        *row = rubraview_settings_view_table_screen_row(v, cstr(FOLDER_LISTS[list].table), (int32_t)folder_list_count(app, list) + 1);
        *col0 = v->content_col;
    } else {
        if (v->focus_line < 0) return false;
        int32_t content_row = v->lines[v->focus_line].row - v->scroll;
        *row = content_row >= 0 && content_row < rubraview_settings_view_visible_rows(v) ? RUBRAVIEW_SETTINGS_TOP_ROWS + content_row : -1;
        *col0 = v->content_col + v->label_cols;
    }
    *cols = v->cols - *col0 - 1;
    return *row >= 0 && *cols >= 8;
}

/* The first character shown: a long path shows its end, where the caret usually is. */
static size_t settings_path_first(const app_state_t *app, int32_t cols) {
    size_t caret_chars = 0;
    for (size_t i = 0; i < app->spath_edit.caret && i < app->spath_edit.len; ++i) {
        if (((unsigned char)app->spath_edit.buf[i] & 0xC0) != 0x80) caret_chars++;
    }
    return caret_chars + 2 > (size_t)cols ? caret_chars + 2 - (size_t)cols : 0;
}

/* A click in the box puts the caret at that character. */
static void settings_path_click(app_state_t *app, int32_t col, int32_t cols) {
    size_t want = settings_path_first(app, cols) + (size_t)(col < 0 ? 0 : col), seen = 0, at = 0;
    const char *t = app->spath_edit.buf;
    while (at < app->spath_edit.len && seen < want) {
        at++;
        while (at < app->spath_edit.len && ((unsigned char)t[at] & 0xC0) == 0x80) at++;
        seen++;
    }
    app->spath_edit.caret = app->spath_edit.anchor = at;
    app->settings_dirty = true;
}

/* The column of a folder row's X, which takes the folder off (owner, 2026-10-16). */
static int32_t skip_dirs_x_col(const app_state_t *app) {
    return app->settings_view.cols - 4;
}

/* An X at the end of each folder kept out, for the mouse. */
static void skip_dirs_draw_x(app_state_t *app) {
    const rubraview_settings_view_t *v = &app->settings_view;
    rubraview_renderer_t *r = app->settings_renderer;
    const double cw = app->settings_cell_w, ch = app->settings_cell_h;
    int32_t mc = (int32_t)(app->settings_tip_x / cw), mr = (int32_t)(app->settings_tip_y / ch);
    for (int32_t l = 0; l < FOLDER_LIST_COUNT; ++l)
    for (size_t k = 1, n = folder_list_count(app, l); k <= n; ++k) {
        int32_t row = rubraview_settings_view_table_screen_row(v, cstr(FOLDER_LISTS[l].table), (int32_t)k);
        if (row < 0) continue;
        rubraview_pal_rect_t cell = { cw * skip_dirs_x_col(app), ch * row, cw * 3, ch };
        bool over = mr == row && mc >= skip_dirs_x_col(app) && mc < skip_dirs_x_col(app) + 3;
        rubraview_pal_render_fill_rect(r, cell, over ? COLOR_CLOSE_HOVER : SETTINGS_PANE, 2.0);
        rubraview_pal_render_draw_text(r, PROVEN_LIT("X"), cell, app->settings_font, SETTINGS_TEXT, RUBRAVIEW_TEXT_CENTER);
    }
}

/* The typing box: its text, the selection, the caret. The list's box is
   there even when nothing is typed, an empty field waiting for a folder. */
static void settings_path_draw(app_state_t *app) {
    rubraview_renderer_t *r = app->settings_renderer;
    const double cw = app->settings_cell_w, ch = app->settings_cell_h, fs = app->settings_font;
    int32_t row = 0, col0 = 0, cols = 0;
    int32_t typing = app->spath_active ? (app->spath_list_index >= 0 ? app->spath_list : -1) : -2;
    for (int32_t l = 0; l < FOLDER_LIST_COUNT; ++l) {   /* waiting: each field's frame round its hint */
        if (l == typing || !settings_path_box(app, l, &row, &col0, &cols)) continue;
        rubraview_pal_render_stroke_rect(r, (rubraview_pal_rect_t){ cw * col0, ch * row, cw * cols, ch }, 0xFF707070u, 1.0, 0.0);
    }
    if (!app->spath_active || !settings_path_box(app, typing, &row, &col0, &cols)) return;
    /* 1.25.0 (owner, 2026-10-20: "에디트 컨트롤과 버튼의 세로 길이가 조금 더 넉넉하게"): a little taller than its row */
    double pad = ch * 0.22;
    double y = ch * row;
    rubraview_pal_rect_t box = { cw * col0, y - pad, cw * cols, ch + 2.0 * pad };
    rubraview_pal_render_fill_rect(r, box, 0xFF101010u, 3.0);
    rubraview_pal_render_stroke_rect(r, box, 0xFF5B9BD5u, 1.0, 3.0);
    proven_u8str_view_t text = rubraview_textedit_text(&app->spath_edit);
    size_t caret_chars = 0, sel_a = 0, sel_b = 0, a = 0, b = 0;
    rubraview_textedit_selection_range(&app->spath_edit, &a, &b);
    for (size_t i = 0; i < text.size; ++i) {
        if (((unsigned char)text.ptr[i] & 0xC0) == 0x80) continue;
        if (i < app->spath_edit.caret) caret_chars++;
        if (i < a) sel_a++;
        if (i < b) sel_b++;
    }
    size_t first = settings_path_first(app, cols);
    size_t skip = 0, seen = 0;
    while (skip < text.size && seen < first) {
        skip++;
        while (skip < text.size && ((unsigned char)text.ptr[skip] & 0xC0) == 0x80) skip++;
        seen++;
    }
    if (sel_b > sel_a) {
        double sx = sel_a > first ? (double)(sel_a - first) : 0.0, ex = (double)(sel_b - first);
        if (ex > sx) rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ box.x + cw * sx, y, cw * (ex - sx), ch }, 0x704A90D9u, 0.0);
    }
    rubraview_pal_render_draw_text_mono(r, (proven_u8str_view_t){ .ptr = text.ptr + skip, .size = text.size - skip }, box.x, y, fs, SETTINGS_TEXT);
    double cx = box.x + cw * (double)(caret_chars - first);
    if (app->spath_composing_length > 0) {
        rubraview_pal_render_draw_text_mono(r, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)app->spath_composing, .size = app->spath_composing_length },
                                            cx, y, fs, 0xFFFFD27Fu);
    }
    rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ cx, y + 2.0, 1.5, ch - 4.0 }, SETTINGS_TEXT, 0.0);
    /* 1.24.0: its buttons on the row under it — OK, Cancel, and Browse... for a folder */
    const char *words[3] = { T("OK"), T("Cancel"), T("Browse...") };
    int32_t n = app->spath_number || (proven_u8str_view_eq(app->spath_section, PROVEN_LIT("files")) && proven_u8str_view_eq(app->spath_key, PROVEN_LIT("pattern"))) ? 2 : 3;
    double bx = box.x, by = box.y + box.height + pad, bh = ch + 2.0 * pad;
    rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ box.x, box.y + box.height, box.width, bh + 2.0 * pad }, SETTINGS_BACKGROUND, 0.0);   /* the rows under it covered */
    app->spath_btn_n = n;
    for (int32_t i = 0; i < n; ++i) {
        /* RV-182 (owner, 2026-10-29: the words were pushed right): as wide as the words measure, plus room either side */
        double tw = 0.0;
        if (!rubraview_pal_render_measure_text(r, cstr(words[i]), fs, &tw) || tw <= 0.0) tw = cw * (double)rubraview_utf8_chars(cstr(words[i]));
        double width = tw + cw * 4.0;
        if (width < cw * 10.0) width = cw * 10.0;
        rubraview_pal_rect_t b = { bx, by, width, bh };
        app->spath_btn[i] = b;
        bool over = pal_rect_has(b, app->settings_px, app->settings_py);
        rubraview_pal_render_fill_rect(r, b, i == 0 ? SETTINGS_FOCUS : over ? 0xFF3A3A3Au : 0xFF2A2A2Au, 3.0);
        rubraview_pal_render_stroke_rect(r, b, 0xFF5B9BD5u, 1.0, 3.0);
        /* the words in its middle, both ways, laid out by their own font */
        rubraview_pal_render_draw_text(r, cstr(words[i]), b, fs, SETTINGS_TEXT, RUBRAVIEW_TEXT_CENTER);
        bx += b.width + cw;
    }
}

size_t settings_pump(app_state_t *app) {
    size_t handled = 0;
    rubraview_window_event_t event;
    /* the keys page: a row rested on says what its action does */
    if (app->settings_open && (app->settings_tip_row > 0 || app->settings_tip_help >= 0) && !app->settings_tip_shown &&
        rubraview_pal_time_now_seconds() - app->settings_tip_since >= 0.6) {
        app->settings_tip_shown = true;
        app->settings_dirty = true;
    }
    while (app->settings_open && rubraview_pal_window_poll_event(app->settings_window, &event)) {
        handled++;
        rubraview_settings_view_t *view = &app->settings_view;
        switch (event.kind) {
            case RUBRAVIEW_WINDOW_EVENT_CLOSE:
                settings_close(app);
                return handled;
            case RUBRAVIEW_WINDOW_EVENT_RESIZE: {
                rubraview_pal_render_resize(app->settings_renderer, event.resize.width, event.resize.height);
                int32_t cols = 0, rows = 0;
                settings_grid(app, &cols, &rows);
                rubraview_settings_view_resize(view, cols, rows);
                app->settings_dirty = true;
                break;
            }
            case RUBRAVIEW_WINDOW_EVENT_DPI_CHANGED: {
                settings_measure(app);
                int32_t cols = 0, rows = 0;
                settings_grid(app, &cols, &rows);
                rubraview_settings_view_resize(view, cols, rows);
                app->settings_dirty = true;
                break;
            }
            case RUBRAVIEW_WINDOW_EVENT_PAINT:
                app->settings_dirty = true;
                break;
            case RUBRAVIEW_WINDOW_EVENT_KEY_DOWN: {
                if (chooser_key(app, CHOOSE_IN_SETTINGS, event.key.combo)) break;   /* 1.24.0 */
                if (app->key_capture > 0) {
                    settings_capture_key(app, event.key.combo);
                    app->settings_dirty = true;
                    break;
                }
                if (settings_path_key(app, event.key.combo)) break;
                if (settings_copy_key(app, event.key.combo)) break;
                rubraview_settings_key_t key;
                if (settings_key_of(event.key.combo, &key)) {
                    settings_event(app, rubraview_settings_view_key(view, &app->settings, key));
                }
                break;
            }
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_DOWN: {
                if (chooser_press(app, CHOOSE_IN_SETTINGS, event.mouse.x, event.mouse.y)) break;   /* 1.24.0 */
                /* RV-170: the scroll bar: its thumb dragged, its track a page, its buttons a row */
                if (event.mouse.button == RUBRAVIEW_MOUSE_LEFT && scrollbar_press(app, RVSB_SETTINGS, event.mouse.x, event.mouse.y)) {
                    app->settings_dirty = true;
                    break;
                }
                if (app->spath_active) {   /* 1.24.0: the box's own buttons */
                    int32_t hit = -1;
                    for (int32_t i = 0; i < app->spath_btn_n; ++i) if (pal_rect_has(app->spath_btn[i], event.mouse.x, event.mouse.y)) hit = i;
                    if (hit == 0) { textbox_insert(&app->spath_edit, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)app->spath_composing, .size = app->spath_composing_length });
                                    app->spath_composing_length = 0; settings_path_commit(app); app->settings_dirty = true; break; }
                    if (hit == 1) { settings_path_end(app); app->settings_dirty = true; break; }
                    if (hit == 2) { settings_path_browse(app); app->settings_dirty = true; break; }
                }
                if (event.mouse.button != RUBRAVIEW_MOUSE_LEFT) break;
                if (app->spath_active) {   /* a click in the box moves the caret; elsewhere keeps the folder as it was */
                    int32_t brow = 0, bcol = 0, bcols = 0;
                    int32_t mc = (int32_t)(event.mouse.x / app->settings_cell_w), mr = (int32_t)(event.mouse.y / app->settings_cell_h);
                    if (settings_path_box(app, app->spath_list_index >= 0 ? app->spath_list : -1, &brow, &bcol, &bcols) &&
                        mr == brow && mc >= bcol && mc < bcol + bcols) {
                        settings_path_click(app, mc - bcol, bcols);
                        break;
                    }
                    settings_path_end(app);
                    settings_say(app, T("folder left as it was"));
                    break;
                }
                if (app->key_capture > 0) {
                    app->key_capture = 0;   /* a click elsewhere is a change of mind */
                    settings_say(app, T("no key added"));
                }
                /* A drag marks text: on a line that is only text, or with
                   Shift anywhere on the page — a plain click on a setting
                   still changes it. */
                int32_t col = (int32_t)(event.mouse.x / app->settings_cell_w);
                int32_t row = (int32_t)(event.mouse.y / app->settings_cell_h);
                if (app->settings_sel.active) { app->settings_sel.active = false; app->settings_dirty = true; }
                if (col >= view->content_col && row != RUBRAVIEW_SETTINGS_BUTTON_ROW &&
                    ((event.mouse.modifiers & RUBRAVIEW_MOD_SHIFT) || settings_row_is_text(app, row))) {
                    int32_t c = col - view->content_col;
                    app->settings_sel = (rubraview_grid_selection_t){ true, row, c, row, c };
                    app->settings_selecting = true;
                    app->settings_dirty = true;
                    break;
                }
                {
                    /* The X at a folder's end takes it off. */
                    int32_t k = rubraview_settings_view_table_row_at(view, col, row), hit = -1;
                    for (int32_t l = 0; l < FOLDER_LIST_COUNT && hit < 0; ++l) {
                        if (k >= 1 && (size_t)k <= folder_list_count(app, l) && col >= skip_dirs_x_col(app) && col < skip_dirs_x_col(app) + 3 &&
                            rubraview_settings_view_table_screen_row(view, cstr(FOLDER_LISTS[l].table), k) == row) hit = l;
                    }
                    if (hit >= 0) {
                        rubraview_settings_view_focus_table_row(view, cstr(FOLDER_LISTS[hit].table), k);
                        settings_event(app, RUBRAVIEW_SEVENT_TABLE_CLEAR);
                        app->settings_dirty = true;
                        break;
                    }
                }
                app->settings_mouse_down = true;
                {
                    /* One click on the folder box starts typing in it. */
                    int32_t box = -1;
                    for (int32_t l = 0; l < FOLDER_LIST_COUNT; ++l) {
                        int32_t last = (int32_t)folder_list_count(app, l) + 1;
                        if (rubraview_settings_view_table_row_at(view, col, row) == last &&
                            rubraview_settings_view_table_screen_row(view, cstr(FOLDER_LISTS[l].table), last) == row) box = l;
                    }
                    rubraview_settings_event_t pressed = rubraview_settings_view_press(view, &app->settings, col, row);
                    if (box >= 0 && folder_list_of_table(rubraview_settings_view_focused_table(view)) == box) pressed = RUBRAVIEW_SEVENT_TABLE_EDIT;
                    settings_event(app, pressed);
                }
                break;
            }
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_MOVE: {
                app->settings_px = event.mouse.x;
                app->settings_py = event.mouse.y;
                if (scrollbar_motion(app, app->settings_window, event.mouse.x, event.mouse.y)) { app->settings_dirty = true; break; }   /* RV-170 */
                if (scrollbar_hover(app, RVSB_SETTINGS, event.mouse.x, event.mouse.y)) app->settings_dirty = true;   /* its parts light */
                if (app->spath_active) app->settings_dirty = true;   /* its buttons light under the pointer */
                int32_t tip = rubraview_settings_view_table_row_at(view, (int32_t)(event.mouse.x / app->settings_cell_w),
                                                                   (int32_t)(event.mouse.y / app->settings_cell_h));
                /* Only the keys' rows say what an action does; the folders' rows are not actions. */
                for (int32_t l = 0; l < FOLDER_LIST_COUNT; ++l) {
                    if (tip > 0 && rubraview_settings_view_table_screen_row(view, cstr(FOLDER_LISTS[l].table), tip) ==
                                       (int32_t)(event.mouse.y / app->settings_cell_h)) tip = -1;
                }
                /* The owner (2026-10-17): resting on a setting's name shows its fuller explanation. */
                int32_t help = -1;
                if (tip <= 0) {
                    int32_t c = (int32_t)(event.mouse.x / app->settings_cell_w), rr = (int32_t)(event.mouse.y / app->settings_cell_h);
                    if (c >= view->content_col && c < view->content_col + view->label_cols) {
                        for (size_t i = 0; i < view->line_count; ++i) {
                            if (view->lines[i].kind != RUBRAVIEW_LINE_SETTING || rubraview_settings_view_screen_row(view, i) != rr) continue;
                            const rubraview_settings_node_t *node = &view->doc->nodes[view->lines[i].node];
                            if (view->doc->defs[node->setting].help.size > 0) help = node->setting;
                            break;
                        }
                    }
                }
                if (tip != app->settings_tip_row || help != app->settings_tip_help) {
                    if (app->settings_tip_shown) app->settings_dirty = true;
                    app->settings_tip_help = help;
                    app->settings_tip_row = tip;
                    app->settings_tip_since = rubraview_pal_time_now_seconds();
                    app->settings_tip_shown = false;
                }
                /* the folders' X lights under the pointer */
                if ((int32_t)(event.mouse.x / app->settings_cell_w) >= skip_dirs_x_col(app) - 1 ||
                    (int32_t)(app->settings_tip_x / app->settings_cell_w) >= skip_dirs_x_col(app) - 1) app->settings_dirty = true;
                app->settings_tip_x = event.mouse.x;
                app->settings_tip_y = event.mouse.y;
            }
                if (app->settings_selecting) {
                    int32_t c = (int32_t)(event.mouse.x / app->settings_cell_w) - view->content_col;
                    int32_t rr = (int32_t)(event.mouse.y / app->settings_cell_h);
                    app->settings_sel.end_col = c < 0 ? 0 : c;
                    app->settings_sel.end_row = rr < 0 ? 0 : rr > view->rows - 1 ? view->rows - 1 : rr;
                    app->settings_dirty = true;
                    break;
                }
                if (app->settings_mouse_down) {
                    settings_event(app, rubraview_settings_view_drag(view, &app->settings,
                                       (int32_t)(event.mouse.x / app->settings_cell_w)));
                }
                break;
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_UP:
                if (scrollbar_release(app, app->settings_window)) { app->settings_dirty = true; break; }   /* RV-170 */
                if (app->settings_selecting) {
                    app->settings_selecting = false;
                    /* A click that did not move marks nothing. */
                    if (app->settings_sel.anchor_row == app->settings_sel.end_row &&
                        app->settings_sel.anchor_col == app->settings_sel.end_col) app->settings_sel.active = false;
                    app->settings_dirty = true;
                    break;
                }
                app->settings_mouse_down = false;
                rubraview_settings_view_release(view);
                break;
            case RUBRAVIEW_WINDOW_EVENT_TEXT:
            case RUBRAVIEW_WINDOW_EVENT_COMPOSITION:
                if (app->spath_active) {
                    proven_u8str_view_t typed = { .ptr = (const proven_byte_t *)event.text.utf8, .size = event.text.length };
                    if (event.kind == RUBRAVIEW_WINDOW_EVENT_TEXT) {
                        textbox_insert(&app->spath_edit, typed);
                        app->spath_composing_length = 0;
                    } else {
                        size_t n = typed.size < sizeof(app->spath_composing) ? typed.size : sizeof(app->spath_composing) - 1;
                        memcpy(app->spath_composing, typed.ptr, n);
                        app->spath_composing_length = n;
                    }
                    app->settings_dirty = true;
                }
                break;
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_WHEEL:
                if (chooser_wheel(app, CHOOSE_IN_SETTINGS, event.mouse.wheel_delta)) break;
                if (app->spath_active) break;   /* the box stays over its row */
                app->settings_sel.active = false;   /* the text under it has moved */
                settings_event(app, rubraview_settings_view_scroll(view, event.mouse.wheel_delta > 0 ? -3 : 3));
                break;
            default:
                break;
        }
        if (!app->settings_open) break;
    }
    return handled;
}

/* The title, the message line, a heading, a note, live information, or
   nothing: a row the pointer can mark from without changing anything. */
bool settings_row_is_text(const app_state_t *app, int32_t row) {
    const rubraview_settings_view_t *v = &app->settings_view;
    if (row < RUBRAVIEW_SETTINGS_TOP_ROWS || row == v->rows - 1) return row != RUBRAVIEW_SETTINGS_BUTTON_ROW;
    int32_t content = row - RUBRAVIEW_SETTINGS_TOP_ROWS + v->scroll;
    for (size_t i = 0; i < v->line_count; ++i) {
        const rubraview_settings_line_t *ln = &v->lines[i];
        if (content < ln->row || content >= ln->row + ln->height) continue;
        return ln->kind == RUBRAVIEW_LINE_SECTION || ln->kind == RUBRAVIEW_LINE_NOTE || ln->kind == RUBRAVIEW_LINE_INFO;
    }
    return true;
}

/* Ctrl+A marks the page; Ctrl+C copies what is marked, or the focused
   line when nothing is. */
bool settings_copy_key(app_state_t *app, rubraview_key_combo_t combo) {
    if (!(combo.modifiers & RUBRAVIEW_MOD_CTRL)) return false;
    const rubraview_settings_view_t *v = &app->settings_view;
    if (key_is(combo, "A")) {
        app->settings_sel = (rubraview_grid_selection_t){ true, 0, 0, v->rows - 1, INT32_MAX / 2 };
        app->settings_dirty = true;
        return true;
    }
    if (!key_is(combo, "C")) return false;
    char text[16384];
    size_t n = 0;
    if (app->settings_sel.active) {
        proven_u8str_view_t rows[96];
        int32_t count = (int32_t)(sizeof(rows) / sizeof(rows[0]));
        for (int32_t i = 0; i < count; ++i) rows[i] = (proven_u8str_view_t){ .ptr = (const proven_byte_t *)app->settings_grid_text[i], .size = app->settings_grid_len[i] };
        n = rubraview_grid_selection_copy(&app->settings_sel, rows, count, text, sizeof(text));
    } else if (v->focus_line >= 0) {
        int32_t row = rubraview_settings_view_screen_row(v, (size_t)v->focus_line);
        if (v->lines[v->focus_line].kind == RUBRAVIEW_LINE_TABLE) row += v->focus_row;
        if (row >= 0 && row < 96) {   /* already a window row, the title's two included */
            proven_u8str_view_t line = { .ptr = (const proven_byte_t *)app->settings_grid_text[row], .size = app->settings_grid_len[row] };
            rubraview_grid_selection_t whole = { true, 0, 0, 0, INT32_MAX / 2 };
            n = rubraview_grid_selection_copy(&whole, &line, 1, text, sizeof(text));
        }
    }
    const char *said = n > 0 && rubraview_pal_clipboard_set_text(app->settings_window, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)text, .size = n })
                           ? T("copied") : T("nothing to copy");
    settings_say(app, said);
    return true;
}

/* A `preview subtitle` line: the sample drawn the way the video will
   draw it, at the size and outline on the page right now. */
static void settings_preview(app_state_t *app, proven_u8str_view_t name, rubraview_pal_rect_t rect) {
    rubraview_renderer_t *r = app->settings_renderer;
    rubraview_pal_render_fill_rect(r, rect, 0xFF303A44u, 0.0);
    if (!proven_u8str_view_eq(name, PROVEN_LIT("subtitle"))) return;
    double dpi = rubraview_pal_window_dpi_scale(app->settings_window);
    double size = rubraview_settings_get(&app->settings, PROVEN_LIT("video"), PROVEN_LIT("subtitle_size")) * dpi;
    double off = rubraview_settings_get(&app->settings, PROVEN_LIT("video"), PROVEN_LIT("subtitle_outline")) * dpi;
    proven_u8str_view_t sample = TU8("Subtitle sample  \xEC\x9E\x90\xEB\xA7\x89 \xEB\xAF\xB8\xEB\xA6\xAC\xEB\xB3\xB4\xEA\xB8\xB0");
    /* A size too tall for the block is drawn as tall as the block
       allows — the video's own subtitle shows the real size. */
    if (size > rect.height * 0.6) size = rect.height * 0.6;
    draw_outlined_text(r, sample, rect, size, off, 0xFFFFFFFFu);
}

/* Text on the settings grid, each narrow run and each wide character at
   its own cell, so Hangul in a folder name does not pull the rest of the
   line off its columns. */
static void settings_text(app_state_t *app, proven_u8str_view_t text, double x, double y, uint32_t color) {
    rubraview_cell_run_t runs[256];
    size_t n = rubraview_cell_runs(text, runs, sizeof(runs) / sizeof(runs[0]));
    for (size_t i = 0; i < n; ++i) {
        proven_u8str_view_t piece = { .ptr = text.ptr + runs[i].offset, .size = runs[i].length };
        if (piece.size == 1 && piece.ptr[0] == ' ') continue;
        rubraview_pal_render_draw_text_mono(app->settings_renderer, piece, x + app->settings_cell_w * runs[i].col,
                                            y, app->settings_font, color);
    }
}

/* The owner (2026-10-17): "설정에서 누를 수 있는 기능은 파란색 글씨와 밑줄로". The part
   of `text` from its first `open` to its last `close` ("< Register >", "[ Close ]",
   a path's "[C:\\...]") in link blue, its words underlined; the rest in `base`. */
static void settings_text_link(app_state_t *app, proven_u8str_view_t text, double x, double y, uint32_t base, char open, char close) {
    size_t a = 0, b = text.size;
    while (a < text.size && text.ptr[a] != open) ++a;
    if (close == 0) {   /* from `open` to the text's end: a box and its word ("[x] on") */
        while (b > a && text.ptr[b - 1] == ' ') --b;
        if (a >= b) { settings_text(app, text, x, y, base); return; }
        proven_u8str_view_t before = { text.ptr, a }, link = { text.ptr + a, b - a };
        double lx = x + app->settings_cell_w * (double)rubraview_text_columns(before);
        settings_text(app, before, x, y, base);
        settings_text(app, link, lx, y, SETTINGS_LINK);
        double line = app->settings_cell_h > 16.0 ? app->settings_cell_h / 16.0 : 1.0;
        rubraview_pal_render_fill_rect(app->settings_renderer, (rubraview_pal_rect_t){ lx, y + app->settings_cell_h - line * 1.5,
                                       app->settings_cell_w * (double)rubraview_text_columns(link), line }, SETTINGS_LINK, 0.0);
        return;
    }
    while (b > a && text.ptr[b - 1] != close) --b;
    if (a >= text.size || b <= a + 1) { settings_text(app, text, x, y, base); return; }
    proven_u8str_view_t before = { text.ptr, a }, link = { text.ptr + a, b - a }, after = { text.ptr + b, text.size - b };
    double cw = app->settings_cell_w, ch = app->settings_cell_h;
    double lx = x + cw * (double)rubraview_text_columns(before);
    settings_text(app, before, x, y, base);
    settings_text(app, link, lx, y, SETTINGS_LINK);
    settings_text(app, after, lx + cw * (double)rubraview_text_columns(link), y, base);
    /* under the words, not the brackets or the spaces inside them */
    size_t i = 1, j = link.size - 1;
    while (i < j && link.ptr[i] == ' ') ++i;
    while (j > i && link.ptr[j - 1] == ' ') --j;
    if (j <= i) return;
    double ux = lx + cw * (double)rubraview_text_columns((proven_u8str_view_t){ link.ptr, i });
    double uw = cw * (double)rubraview_text_columns((proven_u8str_view_t){ link.ptr + i, j - i });
    double line = ch > 16.0 ? ch / 16.0 : 1.0;
    rubraview_pal_render_fill_rect(app->settings_renderer, (rubraview_pal_rect_t){ ux, y + ch - line * 1.5, uw, line }, SETTINGS_LINK, 0.0);
}

/* ---- RV-136: the font picker (owner, 2026-10-18) ----
   A window of its own over the settings, in the program's look: the
   machine's families by name, found by typing; right of each name a short
   sample — I, i, l, 1 and O, 0 side by side, and the language in use —
   drawn first in the program's face and then, a few rows a frame, in the
   family's own as they come into sight, so a thousand fonts never stall
   it. Under the list the weight, italic, a large preview, Default, OK and
   Cancel. What it keeps is "Family[, Weight][, Italic]" (fontspec.h). */

#define FONTPICK_BACKGROUND 0xFF181818u
#define FONTPICK_NEW_FACES  6     /* rows drawn in their own face a frame */

static proven_u8str_view_t fontpick_sample(void) {
    static char buf[96];
    /* "Abc" in a pack is that language's own short word (한글, かな漢字, ...). */
    int n = snprintf(buf, sizeof(buf), "Iil1 aoO0 %s", T("Abc"));
    return (proven_u8str_view_t){ .ptr = (const proven_byte_t *)buf, .size = n > 0 ? (size_t)n : 0 };
}

static double fontpick_dpi(const app_state_t *app) { return rubraview_pal_window_dpi_scale(app->fontpick_window); }
static double fontpick_row_h(const app_state_t *app) { return 30.0 * fontpick_dpi(app); }
static double fontpick_top(const app_state_t *app) { return 46.0 * fontpick_dpi(app); }
static double fontpick_bottom_h(const app_state_t *app) { return 124.0 * fontpick_dpi(app); }

static size_t fontpick_rows(const app_state_t *app) {
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->fontpick_window, &w, &h);
    double room = (double)h - fontpick_top(app) - fontpick_bottom_h(app);
    return room > 0.0 ? (size_t)(room / fontpick_row_h(app)) : 1;
}

/* Does `name` hold the words typed, letters of any case alike? */
static bool fontpick_finds(proven_u8str_view_t name, const char *find, size_t len) {
    if (len == 0) return true;
    for (size_t i = 0; i + len <= name.size; ++i) {
        if (proven_u8str_view_eq_ascii_ci(proven_u8str_view_slice(name, i, len), proven_u8str_view_from_bytes(find, len))) return true;
    }
    return false;
}

static void fontpick_filter(app_state_t *app) {
    app->fontpick_list_count = 0;
    for (size_t i = 0; i < app->fontpick_all_count; ++i) {
        const rubraview_font_family_t *f = &app->fontpick_all[i];
        if (fontpick_finds(f->shown, app->fontpick_find, app->fontpick_find_len) || fontpick_finds(f->name, app->fontpick_find, app->fontpick_find_len))
            app->fontpick_list[app->fontpick_list_count++] = (uint32_t)i;
    }
    if (app->fontpick_sel >= (int32_t)app->fontpick_list_count) app->fontpick_sel = app->fontpick_list_count ? 0 : -1;
    if (app->fontpick_list_count && app->fontpick_sel < 0) app->fontpick_sel = 0;
    app->fontpick_first = 0;
    if (app->fontpick_find_len > 0) {   /* the first family whose name begins with what was typed is the one Enter keeps */
        app->fontpick_sel = app->fontpick_list_count ? 0 : -1;
        for (size_t i = 0; i < app->fontpick_list_count; ++i) {
            const rubraview_font_family_t *f = &app->fontpick_all[app->fontpick_list[i]];
            proven_u8str_view_t a = { f->shown.ptr, f->shown.size < app->fontpick_find_len ? f->shown.size : app->fontpick_find_len };
            proven_u8str_view_t b = { f->name.ptr, f->name.size < app->fontpick_find_len ? f->name.size : app->fontpick_find_len };
            if ((a.size == app->fontpick_find_len && fontpick_finds(a, app->fontpick_find, app->fontpick_find_len)) ||
                (b.size == app->fontpick_find_len && fontpick_finds(b, app->fontpick_find, app->fontpick_find_len))) {
                app->fontpick_sel = (int32_t)i;
                break;
            }
        }
        fontpick_reveal(app);
    }
    app->fontpick_dirty = true;
}

void fontpick_reveal(app_state_t *app) {
    size_t rows = fontpick_rows(app);
    if (app->fontpick_sel < 0) return;
    size_t at = (size_t)app->fontpick_sel;
    if (at < app->fontpick_first) app->fontpick_first = at;
    if (at >= app->fontpick_first + rows) app->fontpick_first = at + 1 - rows;
}

void fontpick_close(app_state_t *app) {
    if (!app->fontpick_open) return;
    if (app->fontpick_renderer) rubraview_pal_render_destroy(app->fontpick_renderer);
    if (app->fontpick_window) rubraview_pal_window_destroy(app->fontpick_window);
    window_block_give(app, &app->fontpick_block);
    app->fontpick_renderer = NULL;
    app->fontpick_window = NULL;
    free(app->fontpick_mem);
    app->fontpick_mem = NULL;
    app->fontpick_all = NULL;
    app->fontpick_list = NULL;
    app->fontpick_drawn = NULL;
    app->fontpick_open = false;
    app->settings_dirty = true;
}

void fontpick_show(app_state_t *app, proven_u8str_view_t section, proven_u8str_view_t key) {
    if (app->fontpick_open) fontpick_close(app);
    size_t cap = 4u << 20;
    app->fontpick_mem = malloc(cap);
    if (!app->fontpick_mem) return;
    proven_arena_t arena = proven_arena_create((proven_mem_mut_t){ .ptr = (proven_byte_t*)app->fontpick_mem, .size = cap });
    app->fontpick_all_count = rubraview_pal_font_families(&arena, &app->fontpick_all);
    proven_result_mem_mut_t list = proven_arena_alloc(&arena, (app->fontpick_all_count + 1) * sizeof(uint32_t));
    proven_result_mem_mut_t drawn = proven_arena_alloc(&arena, app->fontpick_all_count + 1);
    if (app->fontpick_all_count == 0 || !proven_is_ok(list.err) || !proven_is_ok(drawn.err)) {
        free(app->fontpick_mem);
        app->fontpick_mem = NULL;
        settings_say(app, T("no fonts could be listed"));
        return;
    }
    app->fontpick_list = (uint32_t*)(void*)list.value.ptr;
    app->fontpick_drawn = (uint8_t*)drawn.value.ptr;
    memset(app->fontpick_drawn, 0, app->fontpick_all_count + 1);
    rubraview_window_config_t config = { .title = T("Rubraview font"), .width = 680, .height = 600, .owner = app->settings_window };
    app->fontpick_window = rubraview_pal_window_create(window_block_take(app, &app->fontpick_block), &config);
    int32_t w = 0, h = 0;
    if (app->fontpick_window) rubraview_pal_window_get_size(app->fontpick_window, &w, &h);
    app->fontpick_renderer = app->fontpick_window
        ? rubraview_pal_render_create(window_block_arena(app, &app->fontpick_block), rubraview_pal_window_native_handle(app->fontpick_window), w, h) : NULL;
    if (!app->fontpick_renderer) {
        if (app->fontpick_window) rubraview_pal_window_destroy(app->fontpick_window);
        window_block_give(app, &app->fontpick_block);
        app->fontpick_window = NULL;
        free(app->fontpick_mem);
        app->fontpick_mem = NULL;
        settings_say(app, T("the font picker could not open"));
        return;
    }
    app->fontpick_open = true;
    ui_font_apply_all(app);
    rubraview_pal_window_text_input(app->fontpick_window, true);   /* typing finds a family */
    app->fontpick_section = section;
    app->fontpick_key = key;
    rubraview_font_spec_t now = rubraview_font_spec_parse(rubraview_settings_get_text(&app->settings, section, key));
    app->fontpick_weight = now.weight;
    app->fontpick_italic = now.italic;
    app->fontpick_find_len = 0;
    app->fontpick_hover = -1;
    app->fontpick_sel = -1;
    fontpick_filter(app);
    for (size_t i = 0; i < app->fontpick_list_count; ++i) {   /* the font in use, chosen and in sight */
        const rubraview_font_family_t *f = &app->fontpick_all[app->fontpick_list[i]];
        if (now.family.size > 0 && (proven_u8str_view_eq(f->name, now.family) || proven_u8str_view_eq(f->shown, now.family))) {
            app->fontpick_sel = (int32_t)i;
            break;
        }
    }
    size_t rows = fontpick_rows(app);
    if (app->fontpick_sel > 0) app->fontpick_first = (size_t)app->fontpick_sel > rows / 2 ? (size_t)app->fontpick_sel - rows / 2 : 0;
    fontpick_reveal(app);
    app->fontpick_dirty = true;
}

/* What the picker keeps: the family chosen with its weight and italic; empty for the program's face. */
static void fontpick_keep(app_state_t *app, bool program_face) {
    char buf[200];
    proven_u8str_view_t value = PROVEN_LIT("");
    if (!program_face && app->fontpick_sel >= 0 && (size_t)app->fontpick_sel < app->fontpick_list_count) {
        const rubraview_font_family_t *f = &app->fontpick_all[app->fontpick_list[app->fontpick_sel]];
        value = rubraview_font_spec_format(buf, sizeof(buf), f->name, app->fontpick_weight, app->fontpick_italic);
    }
    rubraview_settings_set_text(&app->settings, app->fontpick_section, app->fontpick_key, app_keep(app, value));
    fontpick_close(app);
    settings_took_effect(app);
    settings_say(app, value.size ? T("font chosen") : T("the program's own font"));
}

/* The bottom's places: the weight (its left half lighter, right half heavier), italic, Default, OK, Cancel. */
enum { FONTPICK_WEIGHT, FONTPICK_ITALIC, FONTPICK_DEFAULT, FONTPICK_OK, FONTPICK_CANCEL, FONTPICK_PLACES };
static rubraview_pal_rect_t fontpick_place(const app_state_t *app, int which) {
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->fontpick_window, &w, &h);
    double d = fontpick_dpi(app), y = (double)h - 42.0 * d, bh = 32.0 * d;
    static const double X[FONTPICK_PLACES] = { 12.0, 196.0, 316.0, 436.0, 546.0 };
    static const double W[FONTPICK_PLACES] = { 176.0, 112.0, 112.0, 102.0, 112.0 };
    return (rubraview_pal_rect_t){ X[which] * d, y, W[which] * d, bh };
}

static void fontpick_link(rubraview_renderer_t *r, rubraview_pal_rect_t box, proven_u8str_view_t text, double size, bool lit) {
    if (lit) rubraview_pal_render_fill_rect(r, box, 0xFF2A2F38u, 3.0);
    rubraview_pal_render_stroke_rect(r, box, COLOR_BOX_BORDER, 1.0, 3.0);
    rubraview_pal_render_draw_text(r, text, box, size, SETTINGS_LINK, RUBRAVIEW_TEXT_CENTER);
    double tw = 0.0;
    if (rubraview_pal_render_measure_text(r, text, size, &tw) && tw < box.width) {
        rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ box.x + (box.width - tw) * 0.5, box.y + box.height * 0.5 + size * 0.62,
                                                                  tw, size > 16.0 ? size / 16.0 : 1.0 }, SETTINGS_LINK, 0.0);
    }
}

void draw_fontpick_window(app_state_t *app) {
    if (!app->fontpick_open || !app->fontpick_dirty) return;
    app->fontpick_dirty = false;
    rubraview_renderer_t *r = app->fontpick_renderer;
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->fontpick_window, &w, &h);
    double d = fontpick_dpi(app), rh = fontpick_row_h(app), top = fontpick_top(app), font = 14.0 * d;
    rubraview_pal_render_begin(r, FONTPICK_BACKGROUND);

    /* The words that find a family, typed anywhere in the window. */
    rubraview_pal_rect_t find = { 12.0 * d, 8.0 * d, (double)w - 24.0 * d, 30.0 * d };
    rubraview_pal_render_fill_rect(r, find, 0xFF101010u, 3.0);
    rubraview_pal_render_stroke_rect(r, find, 0xFF5B9BD5u, 1.0, 3.0);
    char line[256];
    int n = app->fontpick_find_len
        ? snprintf(line, sizeof(line), "%.*s|   %zu / %zu", (int)app->fontpick_find_len, app->fontpick_find, app->fontpick_list_count, app->fontpick_all_count)
        : snprintf(line, sizeof(line), T("type to find a font   %zu"), app->fontpick_all_count);
    if (n > 0) rubraview_pal_render_draw_text(r, (proven_u8str_view_t){ (const proven_byte_t *)line, (size_t)n }, (rubraview_pal_rect_t){ find.x + 8.0 * d, find.y, find.width - 16.0 * d, find.height },
                                              font, app->fontpick_find_len ? COLOR_TEXT : 0xFF909090u, RUBRAVIEW_TEXT_LEFT);

    /* The families: the name, then the sample — in its own face once drawn so. */
    size_t rows = fontpick_rows(app), fresh = 0;
    bool waiting = false;
    proven_u8str_view_t sample = fontpick_sample();
    double name_w = (double)w * 0.46;
    for (size_t k = 0; k < rows && app->fontpick_first + k < app->fontpick_list_count; ++k) {
        size_t at = app->fontpick_first + k;
        uint32_t fi = app->fontpick_list[at];
        const rubraview_font_family_t *f = &app->fontpick_all[fi];
        rubraview_pal_rect_t row = { 4.0 * d, top + rh * (double)k, (double)w - 8.0 * d, rh };
        if ((int32_t)at == app->fontpick_sel) rubraview_pal_render_fill_rect(r, row, 0xFF2B4A6Fu, 2.0);
        else if ((int32_t)at == app->fontpick_hover) rubraview_pal_render_fill_rect(r, row, COLOR_TILE_FILL, 2.0);
        char fit[300];
        rubraview_pal_render_draw_text(r, files_fit_text(fit, sizeof(fit), f->shown, name_w - 20.0 * d, font), (rubraview_pal_rect_t){ row.x + 10.0 * d, row.y, name_w - 14.0 * d, rh },
                                       font, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
        rubraview_pal_rect_t right = { name_w, row.y, row.width - name_w - 6.0 * d, rh };
        if (!app->fontpick_drawn[fi] && fresh < FONTPICK_NEW_FACES) { app->fontpick_drawn[fi] = 1; fresh++; }
        if (app->fontpick_drawn[fi]) {
            rubraview_pal_render_set_text_font(r, f->name, app->fontpick_weight, app->fontpick_italic);
            rubraview_pal_render_draw_text(r, sample, right, font * 1.25, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
            rubraview_pal_render_set_text_family(r, PROVEN_LIT(""));
        } else {
            rubraview_pal_render_draw_text(r, sample, right, font * 1.25, 0xFF606060u, RUBRAVIEW_TEXT_LEFT);
            waiting = true;
        }
    }
    if (app->fontpick_list_count == 0) {
        rubraview_pal_render_draw_text(r, TU8("no font has those letters"), (rubraview_pal_rect_t){ 0.0, top, (double)w, rh * 2.0 },
                                       font, 0xFF909090u, RUBRAVIEW_TEXT_CENTER);
    }
    draw_scrollbar(app, r, RVSB_FONTPICK, app->fontpick_px, app->fontpick_py);   /* RV-170: where the rows are, and it moves them */

    /* The preview, the weight and italic, and the buttons. */
    double by = (double)h - fontpick_bottom_h(app);
    rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ 0.0, by, (double)w, (double)h - by }, 0xFF202020u, 0.0);
    rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ 0.0, by, (double)w, 1.0 }, COLOR_BOX_BORDER, 0.0);
    if (app->fontpick_sel >= 0 && (size_t)app->fontpick_sel < app->fontpick_list_count) {
        const rubraview_font_family_t *f = &app->fontpick_all[app->fontpick_list[app->fontpick_sel]];
        rubraview_pal_render_set_text_font(r, f->name, app->fontpick_weight, app->fontpick_italic);
        rubraview_pal_render_draw_text(r, sample, (rubraview_pal_rect_t){ 14.0 * d, by + 6.0 * d, (double)w - 28.0 * d, 70.0 * d },
                                       30.0 * d, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
        rubraview_pal_render_set_text_family(r, PROVEN_LIT(""));
    }
    char weight[64];
    snprintf(weight, sizeof(weight), "<  %s  >", rubraview_font_weight_name(app->fontpick_weight));
    const char *words[FONTPICK_PLACES] = { weight, app->fontpick_italic ? T("[x] Italic") : T("[ ] Italic"), T("Default"), T("OK"), T("Cancel") };
    for (int i = 0; i < FONTPICK_PLACES; ++i) {
        rubraview_pal_rect_t b = fontpick_place(app, i);
        bool lit = app->fontpick_px >= b.x && app->fontpick_px < b.x + b.width && app->fontpick_py >= b.y && app->fontpick_py < b.y + b.height;
        fontpick_link(r, b, cstr(words[i]), font, lit);
    }
    if (!rubraview_pal_render_end(r)) { fontpick_close(app); return; }
    if (waiting) app->fontpick_dirty = true;   /* the next frame draws a few more in their own face */
}

static int32_t fontpick_row_at(const app_state_t *app, double y) {
    double top = fontpick_top(app);
    if (y < top) return -1;
    size_t k = (size_t)((y - top) / fontpick_row_h(app));
    if (k >= fontpick_rows(app) || app->fontpick_first + k >= app->fontpick_list_count) return -1;
    return (int32_t)(app->fontpick_first + k);
}

/* RV-170: the list's scroll bar, at the window's right edge beside the rows. */
bool fontpick_sb(app_state_t *app, rubraview_scrollbar_t *sb, double bw, double dpi) {
    if (!app->fontpick_open) return false;
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->fontpick_window, &w, &h);
    size_t rows = fontpick_rows(app);
    *sb = (rubraview_scrollbar_t){ .area = { (double)w - bw - 2.0 * dpi, fontpick_top(app), bw, fontpick_row_h(app) * (double)rows },
                                   .button = bw, .total = (double)app->fontpick_list_count, .shown = (double)rows,
                                   .first = (double)app->fontpick_first, .step = 1.0, .min_thumb = 16.0 * dpi };
    return true;
}

static void fontpick_scroll(app_state_t *app, long rows);
void fontpick_sb_to(app_state_t *app, size_t first) {
    fontpick_scroll(app, (long)first - (long)app->fontpick_first);
}

static void fontpick_scroll(app_state_t *app, long rows) {
    size_t shown = fontpick_rows(app), count = app->fontpick_list_count;
    long first = (long)app->fontpick_first + rows;
    long last = count > shown ? (long)(count - shown) : 0;
    app->fontpick_first = (size_t)(first < 0 ? 0 : first > last ? last : first);
    app->fontpick_dirty = true;
}

static void fontpick_step_weight(app_state_t *app, int dir) {
    int32_t at = 3;
    for (int32_t i = 0; i < RUBRAVIEW_FONT_WEIGHTS; ++i) if (rubraview_font_weight_at(i) == app->fontpick_weight) at = i;
    app->fontpick_weight = rubraview_font_weight_at(at + dir);
    memset(app->fontpick_drawn, 0, app->fontpick_all_count);   /* the samples again, in the new weight */
    app->fontpick_dirty = true;
}

size_t fontpick_pump(app_state_t *app) {
    size_t handled = 0;
    rubraview_window_event_t event;
    while (app->fontpick_open && rubraview_pal_window_poll_event(app->fontpick_window, &event)) {
        handled++;
        switch (event.kind) {
            case RUBRAVIEW_WINDOW_EVENT_CLOSE:
                fontpick_close(app);
                return handled;
            case RUBRAVIEW_WINDOW_EVENT_RESIZE:
                rubraview_pal_render_resize(app->fontpick_renderer, event.resize.width, event.resize.height);
                fontpick_reveal(app);
                app->fontpick_dirty = true;
                break;
            case RUBRAVIEW_WINDOW_EVENT_PAINT:
            case RUBRAVIEW_WINDOW_EVENT_DPI_CHANGED:
                app->fontpick_dirty = true;
                break;
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_UP:
                if (scrollbar_release(app, app->fontpick_window)) app->fontpick_dirty = true;   /* RV-170 */
                break;
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_MOVE: {
                int32_t was = app->fontpick_hover;
                app->fontpick_px = event.mouse.x;
                app->fontpick_py = event.mouse.y;
                if (scrollbar_motion(app, app->fontpick_window, event.mouse.x, event.mouse.y)) { app->fontpick_dirty = true; break; }   /* RV-170 */
                app->fontpick_hover = fontpick_row_at(app, event.mouse.y);
                if (app->fontpick_hover != was || event.mouse.y > (double)fontpick_top(app)) app->fontpick_dirty = true;
                break;
            }
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_WHEEL:
                fontpick_scroll(app, event.mouse.wheel_delta > 0 ? -3 : 3);
                app->fontpick_hover = fontpick_row_at(app, event.mouse.y);
                break;
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_DOWN: {
                if (event.mouse.button != RUBRAVIEW_MOUSE_LEFT) break;
                if (scrollbar_press(app, RVSB_FONTPICK, event.mouse.x, event.mouse.y)) { app->fontpick_dirty = true; break; }   /* RV-170 */
                int32_t row = fontpick_row_at(app, event.mouse.y);
                if (row >= 0) {
                    static double last_click = -1.0;   /* a second click on the chosen row soon after keeps it */
                    double now = rubraview_pal_time_now_seconds();
                    if (row == app->fontpick_sel && last_click >= 0.0 && now - last_click < 0.45) { last_click = -1.0; fontpick_keep(app, false); return handled; }
                    last_click = now;
                    app->fontpick_sel = row;
                    app->fontpick_dirty = true;
                    break;
                }
                for (int i = 0; i < FONTPICK_PLACES; ++i) {
                    rubraview_pal_rect_t b = fontpick_place(app, i);
                    if (!(event.mouse.x >= b.x && event.mouse.x < b.x + b.width && event.mouse.y >= b.y && event.mouse.y < b.y + b.height)) continue;
                    if (i == FONTPICK_WEIGHT) fontpick_step_weight(app, event.mouse.x < b.x + b.width * 0.5 ? -1 : +1);
                    else if (i == FONTPICK_ITALIC) { app->fontpick_italic = !app->fontpick_italic; memset(app->fontpick_drawn, 0, app->fontpick_all_count); }
                    else if (i == FONTPICK_DEFAULT) { fontpick_keep(app, true); return handled; }
                    else if (i == FONTPICK_OK) { fontpick_keep(app, false); return handled; }
                    else { fontpick_close(app); return handled; }
                    app->fontpick_dirty = true;
                }
                break;
            }
            case RUBRAVIEW_WINDOW_EVENT_TEXT: {
                /* Typing finds a family; a control character is a key's, not a letter. */
                for (size_t i = 0; i < event.text.length && app->fontpick_find_len + 1 < sizeof(app->fontpick_find); ++i) {
                    unsigned char c = (unsigned char)event.text.utf8[i];
                    if (c < 0x20 || c == 0x7F) continue;
                    app->fontpick_find[app->fontpick_find_len++] = (char)c;
                }
                fontpick_filter(app);
                break;
            }
            case RUBRAVIEW_WINDOW_EVENT_KEY_DOWN: {
                rubraview_key_combo_t combo = event.key.combo;
                bool ctrl = (combo.modifiers & RUBRAVIEW_MOD_CTRL) != 0;
                long rows = (long)fontpick_rows(app), step = 0;
                if (key_is(combo, "Escape")) { fontpick_close(app); return handled; }
                if (key_is(combo, "Enter")) { fontpick_keep(app, false); return handled; }
                if (key_is(combo, "Backspace")) {
                    while (app->fontpick_find_len > 0 && ((unsigned char)app->fontpick_find[app->fontpick_find_len - 1] & 0xC0) == 0x80) app->fontpick_find_len--;
                    if (app->fontpick_find_len > 0) app->fontpick_find_len--;
                    fontpick_filter(app);
                    break;
                }
                if (ctrl && key_is(combo, "B")) { fontpick_step_weight(app, app->fontpick_weight >= 600 ? -3 : +3); break; }
                if (ctrl && key_is(combo, "I")) { app->fontpick_italic = !app->fontpick_italic; memset(app->fontpick_drawn, 0, app->fontpick_all_count); app->fontpick_dirty = true; break; }
                if (key_is(combo, "Left")) { fontpick_step_weight(app, -1); break; }
                if (key_is(combo, "Right")) { fontpick_step_weight(app, +1); break; }
                if (key_is(combo, "Up")) step = -1;
                else if (key_is(combo, "Down")) step = 1;
                else if (key_is(combo, "PageUp")) step = -(rows > 1 ? rows - 1 : 1);
                else if (key_is(combo, "PageDown")) step = rows > 1 ? rows - 1 : 1;
                else if (key_is(combo, "Home")) step = -(long)app->fontpick_list_count;
                else if (key_is(combo, "End")) step = (long)app->fontpick_list_count;
                if (step != 0 && app->fontpick_list_count > 0) {
                    long to = (long)(app->fontpick_sel < 0 ? 0 : app->fontpick_sel) + step;
                    if (to < 0) to = 0;
                    if (to >= (long)app->fontpick_list_count) to = (long)app->fontpick_list_count - 1;
                    app->fontpick_sel = (int32_t)to;
                    fontpick_reveal(app);
                    app->fontpick_dirty = true;
                }
                break;
            }
            default:
                break;
        }
    }
    return handled;
}

static void settings_grid_keep(app_state_t *app, int32_t row, proven_u8str_view_t text) {
    if (row < 0 || row >= (int32_t)(sizeof(app->settings_grid_len) / sizeof(app->settings_grid_len[0]))) return;
    size_t n = text.size < sizeof(app->settings_grid_text[0]) ? text.size : sizeof(app->settings_grid_text[0]) - 1;
    /* Cut on a character, never inside one. */
    while (n > 0 && n < text.size && ((unsigned char)text.ptr[n] & 0xC0u) == 0x80u) --n;
    memcpy(app->settings_grid_text[row], text.ptr, n);
    app->settings_grid_len[row] = (uint16_t)n;
}

/* A table row with a box to tick in it (an extension's): "[x]" or "[ ]". */
static bool settings_row_has_box(proven_u8str_view_t text) {
    for (size_t i = 0; i + 2 < text.size; ++i)
        if (text.ptr[i] == '[' && (text.ptr[i + 1] == 'x' || text.ptr[i + 1] == ' ') && text.ptr[i + 2] == ']') return true;
    return false;
}

/* ---- 1.24.0: the one-of-many list (owner, 2026-10-19: "여러개 중에 하나를 선택해야 되는 기능이라면 ...
   하위요소 팝업창이 떠서 그 안에서 선택"). A tile's list has rows for a finger; the settings window's is
   a list of text. The keys that step through a choice (B, Shift+A ...) still step. ---- */

static rubraview_window_t *chooser_window(const app_state_t *app, int where) {
    return where == CHOOSE_IN_SETTINGS ? app->settings_window : where == CHOOSE_IN_EDIT ? app->edit_window
         : where == CHOOSE_IN_FILES ? app->files_window : app->window;
}

static void chooser_word(chooser_t *c, double value, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void chooser_word(chooser_t *c, double value, const char *fmt, ...) {
    if (c->count >= CHOOSE_MAX) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(c->words[c->count], sizeof(c->words[0]), fmt, ap);
    va_end(ap);
    c->values[c->count++] = value;
}

static void chooser_dirty(app_state_t *app) {
    if (app->chooser.where == CHOOSE_IN_SETTINGS) app->settings_dirty = true;
    else if (app->chooser.where == CHOOSE_IN_EDIT) app->edit_dirty = true;
    else if (app->chooser.where == CHOOSE_IN_FILES) app->files_dirty = true;
    app->toolbox_window_drawn = 0.0;
}

void chooser_close(app_state_t *app) {
    if (!app->chooser.open) return;
    chooser_dirty(app);
    app->chooser.open = false;
}

/* Laid out under (`at_x`, `at_y`), or over `above_y` when there is no room under it. */
static void chooser_show(app_state_t *app, int where, double at_x, double at_y, double above_y) {
    chooser_t *c = &app->chooser;
    if (c->count == 0) { c->open = false; return; }
    rubraview_window_t *win = chooser_window(app, where);
    if (!win) { c->open = false; return; }
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(win, &w, &h);
    double row_h, width;
    if (where == CHOOSE_IN_SETTINGS) {   /* a list of text, a line a choice */
        size_t longest = strlen(c->title);
        for (size_t i = 0; i < c->count; ++i) { size_t n = strlen(c->words[i]); if (n > longest) longest = n; }
        row_h = app->settings_cell_h;
        width = app->settings_cell_w * (double)(longest + 6);
    } else {                             /* for a finger */
        double dpi = rubraview_pal_window_dpi_scale(win);
        row_h = 44.0 * dpi;
        width = 300.0 * dpi;
    }
    c->where = where;
    c->focus = c->current;
    c->layout = rubraview_chooser_place((double)w, (double)h, c->count, row_h, width, at_x, at_y, above_y,
                                        c->current >= 0 ? (size_t)c->current : 0);
    c->open = true;
    chooser_dirty(app);
}

static int32_t chooser_nearest(const chooser_t *c, double value) {
    int32_t best = -1;
    for (size_t i = 0; i < c->count; ++i)
        if (best < 0 || fabs(c->values[i] - value) < fabs(c->values[best] - value)) best = (int32_t)i;
    return best;
}

/* A tile that chooses one of several: its list instead of the next one. False for any other tile. */
bool chooser_open_tile(app_state_t *app, proven_u8str_view_t action, double x, double y, double above_y) {
    chooser_t *c = &app->chooser;
    *c = (chooser_t){ .kind = CHOOSE_TILE, .current = -1 };
    if (action.size >= sizeof(c->action)) return false;
    memcpy(c->action, action.ptr, action.size);
    const char *title = NULL;
    if (proven_u8str_view_eq(action, PROVEN_LIT("zoom_cycle"))) {
        title = T("Zoom");
        chooser_word(c, 0.0, "%s", T("Real size"));
        for (int p = 20; p <= 200; p += 20) chooser_word(c, (double)p, "%d%%", p);
        c->current = at_real_size(app) ? 0 : chooser_nearest(c, app->zoom * 100.0);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("fit_cycle"))) {
        title = T("Fit");
        chooser_word(c, RUBRAVIEW_FIT_WINDOW, "%s", T("fit: the window"));
        chooser_word(c, RUBRAVIEW_FIT_WIDTH, "%s", T("fit: the width"));
        chooser_word(c, RUBRAVIEW_FIT_HEIGHT, "%s", T("fit: the height"));
        chooser_word(c, RUBRAVIEW_FIT_SMART, "%s", T("fit: smart (never enlarged)"));
        chooser_word(c, RUBRAVIEW_FIT_STRETCH, "%s", T("fit: stretched to fill"));
        c->current = chooser_nearest(c, (double)app->fit_mode);
        if (fabs(c->values[c->current] - (double)app->fit_mode) > 0.5) c->current = -1;
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("toggle_layout"))) {
        title = T("Layout");
        for (int i = 0; i < (int)RUBRAVIEW_LAYOUT_CHOICE_COUNT; ++i) chooser_word(c, i, "%s", layout_said((rubraview_layout_choice_t)i));
        c->current = (int32_t)rubraview_layout_choice_of(app->layout_opts.mode, app->layout_opts.direction);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("media_speed_cycle"))) {
        title = T("Speed");
        static const double SPEEDS[] = { 0.5, 0.75, 1.0, 1.25, 1.5, 2.0 };
        for (size_t i = 0; i < 6; ++i) chooser_word(c, SPEEDS[i], "%gx", SPEEDS[i]);
        c->current = chooser_nearest(c, app->media_speed > 0.0 ? app->media_speed : 1.0);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("interval_cycle"))) {
        title = T("Delay");
        static const double DELAYS[] = { 1.0, 2.0, 3.0, 5.0, 10.0 };
        for (size_t i = 0; i < 5; ++i) chooser_word(c, DELAYS[i], T("%g s"), DELAYS[i]);
        c->current = chooser_nearest(c, app->slideshow.interval_seconds);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("media_repeat_cycle")) && anim_plays(app)) {
        title = T("At the end");
        chooser_word(c, 0, "%s", T("an animation goes round and round"));
        chooser_word(c, 1, "%s", T("an animation plays once and stops"));
        chooser_word(c, 2, "%s", T("after an animation, the next file"));
        c->current = anim_end_mode(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("media_repeat_cycle"))) {
        title = T("At the end");
        for (int i = 0; i < (int)RUBRAVIEW_REPEAT_MODE_COUNT; ++i) chooser_word(c, i, "%s", rubraview_repeat_sentence((rubraview_repeat_mode_t)i));
        c->current = (int32_t)repeat_mode(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("media_eq_cycle"))) {
        title = T("Equaliser");
        for (int i = 0; i < RUBRAVIEW_EQWIN_PRESETS; ++i) { proven_u8str_view_t n = eq_preset_label(i); chooser_word(c, i, "%.*s", (int)n.size, n.ptr); }
        c->current = eq_preset_now(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("media_visualizer_cycle"))) {
        title = T("Analyser");
        chooser_word(c, 0, "%s", T("analyser off"));
        chooser_word(c, 1, "%s", T("analyser: spectrum"));
        chooser_word(c, 2, "%s", T("analyser: oscilloscope"));
        c->current = (int32_t)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("audio"), PROVEN_LIT("visualizer")));
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("text_breaks")) && app->text) {
        title = T("Line breaks");
        chooser_word(c, 0, "%s", T("line breaks as they are"));
        chooser_word(c, 1, "%s", T("all in one run, no line breaks"));
        chooser_word(c, 2, "%s", T("blank lines dropped"));
        chooser_word(c, 3, "%s", T("paragraphs found (a sentence's end and more line breaks)"));
        c->current = (int32_t)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("text"), PROVEN_LIT("breaks")));
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("text_encoding")) && app->text) {
        title = T("Encoding");
        for (int i = 0; i < (int)RUBRAVIEW_TEXTENC_COUNT; ++i) {
            if (i == (int)RUBRAVIEW_TEXTENC_AUTO) continue;
            chooser_word(c, i, "%s", rubraview_textenc_name((rubraview_textenc_t)i));
        }
        c->current = chooser_nearest(c, (double)text_enc_now(app));
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("next_audio_track")) && app->media) {
        title = T("Sound track");
        for (size_t i = 0; i < app->tracks.count; ++i) {
            if (app->tracks.tracks[i].kind != RUBRAVIEW_TRACK_AUDIO) continue;
            char label[160];
            proven_u8str_view_t text = rubraview_track_label(label, sizeof(label), &app->tracks, (int32_t)i);
            if ((int32_t)i == app->tracks.current_audio) c->current = (int32_t)c->count;
            chooser_word(c, (double)i, "%.*s", (int)text.size, text.ptr);
        }
        if (c->count < 2) { osd_say(app, TU8("this file has only one sound track")); c->count = 0; return true; }
    } else {
        return false;
    }
    snprintf(c->title, sizeof(c->title), "%s", title);
    chooser_show(app, CHOOSE_IN_MAIN, x, y, above_y);
    return true;
}

static void chooser_pick_tile(app_state_t *app, size_t i) {
    chooser_t *c = &app->chooser;
    proven_u8str_view_t action = cstr(c->action);
    double v = c->values[i];
    if (proven_u8str_view_eq(action, PROVEN_LIT("zoom_cycle"))) {
        if (v == 0.0) {
            if (app->fit_mode != RUBRAVIEW_FIT_ACTUAL_SIZE) app->fit_beside_real = app->fit_mode;
            app->fit_mode = RUBRAVIEW_FIT_ACTUAL_SIZE;
            reset_view(app);
        } else {
            zoom_leave_real_fit(app);
            app->zoom = v / 100.0;
        }
        zoom_say(app);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("fit_cycle"))) {
        app->fit_mode = (rubraview_fit_mode_t)(int)v;
        reset_view(app);
        osd_say(app, cstr(c->words[i]));
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("toggle_layout"))) {
        layout_set(app, (rubraview_layout_choice_t)(int)v, true);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("media_speed_cycle"))) {
        speed_set(app, v);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("interval_cycle"))) {
        interval_set(app, v);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("media_repeat_cycle")) && anim_plays(app)) {
        rubraview_settings_set(&app->settings, PROVEN_LIT("viewer"), PROVEN_LIT("animation_end"), v);
        osd_say(app, cstr(c->words[i]));
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("media_repeat_cycle"))) {
        rubraview_settings_set(&app->settings, PROVEN_LIT("audio"), PROVEN_LIT("at_end"), v);
        app->follow_for = -1;   /* decided again, in the new mode */
        osd_say(app, cstr(c->words[i]));
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("media_eq_cycle"))) {
        rubraview_settings_set(&app->settings, PROVEN_LIT("audio"), PROVEN_LIT("eq_preset"), v);
        audio_chain_update(app);
        char line[96];
        int n = snprintf(line, sizeof(line), T("equaliser: %s"), c->words[i]);
        if (n > 0) osd_say(app, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)line, .size = (size_t)n });
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("media_visualizer_cycle"))) {
        rubraview_settings_set(&app->settings, PROVEN_LIT("audio"), PROVEN_LIT("visualizer"), v);
        osd_say(app, cstr(c->words[i]));
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("text_breaks")) && app->text) {
        rubraview_settings_set(&app->settings, PROVEN_LIT("text"), PROVEN_LIT("breaks"), v);
        if (!text_rebuild(app)) { osd_say(app, TU8("out of memory")); return; }
        osd_say(app, cstr(c->words[i]));
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("text_encoding")) && app->text) {
        text_encoding_set(app, (rubraview_textenc_t)(int)v);
    } else if (proven_u8str_view_eq(action, PROVEN_LIT("next_audio_track"))) {
        audio_track_set(app, (int32_t)v);
    }
}

/* 1.25.0: the wheel over a one-of-many tile: the choice before or after the one in force, round. */
bool chooser_wheel_tile(app_state_t *app, proven_u8str_view_t action, double notches) {
    if (proven_u8str_view_eq(action, PROVEN_LIT("next_audio_track")) || notches == 0.0) return false;   /* tracks: the list */
    bool was_open = app->chooser.open;
    chooser_t keep = app->chooser;
    if (!chooser_open_tile(app, action, 0.0, 0.0, 0.0)) { app->chooser = keep; return false; }
    chooser_t *c = &app->chooser;
    c->open = false;
    if (c->count == 0) { if (was_open) app->chooser = keep; return true; }
    int32_t n = (int32_t)c->count, at = c->current < 0 ? 0 : c->current;
    at = ((at + (notches > 0.0 ? -1 : 1)) % n + n) % n;   /* away from you: the one before */
    chooser_pick_tile(app, (size_t)at);
    app->chooser.open = false;
    return true;
}

/* A panel's choice (print, convert, export, Edit): its words, as the row says them for each value. */
void chooser_open_panel(app_state_t *app, int32_t which, int32_t row, double x, double y, double above_y) {
    rubraview_panel_t *panel = which == 2 ? &app->sel_panel : &app->panel;
    if (row < 0 || (size_t)row >= panel->row_count || panel->rows[row].kind != RUBRAVIEW_ROW_CHOICE) return;
    chooser_t *c = &app->chooser;
    *c = (chooser_t){ .kind = CHOOSE_PANEL, .panel = which, .row = row, .current = (int32_t)lround(panel->rows[row].value) };
    snprintf(c->title, sizeof(c->title), "%.*s", (int)panel->rows[row].label.size, panel->rows[row].label.ptr);
    int32_t n = panel->rows[row].choice_count > 0 ? panel->rows[row].choice_count : (int32_t)panel->rows[row].max_value + 1;
    for (int32_t i = 0; i < n && i < CHOOSE_MAX; ++i) {
        rubraview_panel_row_t probe = panel->rows[row];
        probe.value = (double)i;
        char words[96];
        proven_u8str_view_t text = which == 1 && (app->panel_is_print || app->panel_is_convert) ? print_row_text(&probe, words, sizeof(words))
                                                                                     : adjust_row_text(app, &probe, words, sizeof(words));
        chooser_word(c, (double)i, "%.*s", (int)text.size, text.ptr);
    }
    chooser_show(app, app->edit_window && edit_panel_open(app) ? CHOOSE_IN_EDIT : CHOOSE_IN_MAIN, x, y, above_y);
}

/* A setting's choice: its words, a line each, under its row. */
void chooser_open_setting(app_state_t *app) {
    const rubraview_settings_view_t *v = &app->settings_view;
    const rubraview_setting_def_t *def = rubraview_settings_view_focused_def(v);
    if (!def || def->type != RUBRAVIEW_SETTING_CHOICE) return;
    chooser_t *c = &app->chooser;
    *c = (chooser_t){ .kind = CHOOSE_SETTING, .def = def,
                      .current = (int32_t)lround(rubraview_settings_get(&app->settings, def->section, def->key)) };
    proven_u8str_view_t label = rubraview_tr_u8(def->label);
    snprintf(c->title, sizeof(c->title), "%.*s", (int)label.size, label.ptr);
    for (int32_t i = 0; i < def->choice_count && i < CHOOSE_MAX; ++i) {
        proven_u8str_view_t w = rubraview_settings_choice_word(def, i);
        chooser_word(c, (double)i, "%.*s", (int)w.size, w.ptr);
    }
    int32_t row = rubraview_settings_view_screen_row(v, (size_t)v->focus_line);
    double x = app->settings_cell_w * (double)(v->content_col + v->label_cols);
    chooser_show(app, CHOOSE_IN_SETTINGS, x, app->settings_cell_h * (double)(row + 1), app->settings_cell_h * (double)row);
}

/* The open-file screen's sorting: by which column, and which way. */
void chooser_open_sort(app_state_t *app, bool order, double x, double y, double above_y) {
    chooser_t *c = &app->chooser;
    *c = (chooser_t){ .kind = order ? CHOOSE_ORDER : CHOOSE_SORT };
    int key = (int)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("files"), PROVEN_LIT("picker_sort"))) - 1;
    bool desc = rubraview_settings_get(&app->settings, PROVEN_LIT("files"), PROVEN_LIT("picker_sort_desc")) > 0.5;
    if (order) {
        snprintf(c->title, sizeof(c->title), "%s", T("Order"));
        chooser_word(c, 0, "%s \xE2\x96\xB2", T("Rising"));
        chooser_word(c, 1, "%s \xE2\x96\xBC", T("Falling"));
        c->current = desc ? 1 : 0;
    } else {
        snprintf(c->title, sizeof(c->title), "%s", T("Sort by"));
        for (int k = 0; k < RUBRAVIEW_PICKER_SORT_COUNT; ++k) chooser_word(c, k, "%s", rubraview_picker_sort_name((rubraview_picker_sort_key_t)k));
        c->current = key;
    }
    chooser_show(app, CHOOSE_IN_MAIN, x, y, above_y);
}

/* 1.25.0: the list's .ext — every extension in it, each with its box; a pick turns one on or off
   and the list stays open. The first row shows them all again. */
void chooser_open_listext(app_state_t *app, int where, double x, double y, double above_y) {
    chooser_t *c = &app->chooser;
    size_t first = c->open && c->kind == CHOOSE_LISTEXT ? c->layout.first : 0;
    *c = (chooser_t){ .kind = CHOOSE_LISTEXT, .current = -1 };
    snprintf(c->title, sizeof(c->title), "%s", T("Extensions shown"));
    chooser_word(c, -1.0, "%s", T("All"));
    char seen[CHOOSE_MAX][16];
    size_t counts[CHOOSE_MAX] = {0}, kinds = 0, count = page_count(app);
    for (size_t i = 0; i < count; ++i) {
        proven_u8str_view_t e = rubraview_path_ext(app->source.pages[i].path);
        if (e.size == 0 || e.size >= sizeof(seen[0])) e = PROVEN_LIT(".");
        size_t k = 0;
        for (; k < kinds; ++k) {
            proven_u8str_view_t have = cstr(seen[k]);
            if (proven_u8str_view_eq_ascii_ci(have, e)) break;
        }
        if (k == kinds) {
            if (kinds + 1 >= CHOOSE_MAX) continue;
            for (size_t j = 0; j < e.size; ++j) seen[kinds][j] = rubraview_ascii_lower(e.ptr[j]);
            seen[kinds][e.size] = '\0';
            kinds++;
        }
        counts[k]++;
    }
    for (size_t k = 0; k < kinds; ++k)
        chooser_word(c, (double)k, "%s  %s  (%d)", list_ext_is_off(app, cstr(seen[k])) ? "\xE2\x98\x90" : "\xE2\x98\x91",
                     seen[k][1] ? seen[k] : T("(none)"), (int)counts[k]);
    memcpy(app->list_ext_seen, seen, sizeof(seen));
    chooser_show(app, where, x, y, above_y);
    if (first > 0 && first < c->count) c->layout.first = first;
}

/* RV-192: Shape — wide, tall, square, each with how many are picked of how many; a pick takes all
   of that shape, or lets them go when all already were, and the list stays open. */
void chooser_open_shape(app_state_t *app, int where, double x, double y, double above_y) {
    uint8_t *shapes = NULL;
    size_t *rows = NULL, n = 0;
    bool *selected = NULL;
    if (!shape_table(app, app->shape_for, &shapes, &rows, &n, &selected)) return;
    size_t total[RUBRAVIEW_SHAPE_COUNT], picked[RUBRAVIEW_SHAPE_COUNT];
    rubraview_shape_count(selected, shapes, rows, n, total, picked);
    free(shapes);
    free(rows);
    chooser_t *c = &app->chooser;
    *c = (chooser_t){ .kind = CHOOSE_SHAPE, .current = -1 };
    snprintf(c->title, sizeof(c->title), "%s", T("Pick by shape"));
    for (int k = RUBRAVIEW_SHAPE_WIDE; k < RUBRAVIEW_SHAPE_COUNT; ++k) {
        bool all = total[k] > 0 && picked[k] == total[k];
        chooser_word(c, (double)k, "%s  %s  %d / %d", all ? "\xE2\x98\x91" : "\xE2\x98\x90", shape_name(k), (int)picked[k], (int)total[k]);
    }
    if (total[RUBRAVIEW_SHAPE_UNKNOWN] > 0)   /* folders, music, text, what could not be read: never picked by shape */
        chooser_word(c, 0.0, "    %s  (%d)", shape_name(RUBRAVIEW_SHAPE_UNKNOWN), (int)total[RUBRAVIEW_SHAPE_UNKNOWN]);
    chooser_show(app, where, x, y, above_y);
}

static void list_ext_toggle(app_state_t *app, int32_t k) {
    if (k < 0) { app->list_ext_off_n = 0; list_view_changed(app); return; }
    proven_u8str_view_t e = cstr(app->list_ext_seen[k]);
    for (size_t i = 0; i < app->list_ext_off_n; ++i) {
        if (!proven_u8str_view_eq(cstr(app->list_ext_off[i]), e)) continue;
        memmove(app->list_ext_off[i], app->list_ext_off[i + 1], (app->list_ext_off_n - i - 1) * sizeof(app->list_ext_off[0]));
        app->list_ext_off_n--;
        list_view_changed(app);
        return;
    }
    if (app->list_ext_off_n >= sizeof(app->list_ext_off) / sizeof(app->list_ext_off[0]) || e.size >= sizeof(app->list_ext_off[0])) return;
    memcpy(app->list_ext_off[app->list_ext_off_n], e.ptr, e.size);
    app->list_ext_off[app->list_ext_off_n][e.size] = '\0';
    app->list_ext_off_n++;
    list_view_changed(app);
}

const char *list_sort_word(int key) {
    switch (key) {
        case 1: return T("Name");
        case 2: return T("Extension");
        case 3: return T("Size");
        case 4: return T("Modified");
        case 5: return T("Created");
        default: return T("Sort");
    }
}

void chooser_open_listsort(app_state_t *app, int where, double x, double y, double above_y) {
    chooser_t *c = &app->chooser;
    *c = (chooser_t){ .kind = CHOOSE_LISTSORT, .current = app->list_sort_key - 1 };
    snprintf(c->title, sizeof(c->title), "%s", T("Sort the list by"));
    for (int k = 1; k <= 5; ++k) chooser_word(c, (double)k, "%s", list_sort_word(k));
    chooser_show(app, where, x, y, above_y);
}

/* The list put in order by `key`, rising or falling; the file on screen stays. */
static struct list_sort_key { proven_u8str_view_t name, ext; uint64_t size; int64_t mtime, ctime; } *g_list_keys;
static int g_list_key, g_list_desc;
static int list_sort_cmp(const void *pa, const void *pb) {
    size_t a = *(const size_t*)pa, b = *(const size_t*)pb;
    const struct list_sort_key *x = &g_list_keys[a], *y = &g_list_keys[b];
    int r = 0;
    switch (g_list_key) {
        case 2: r = rubraview_str_lexcmp(x->ext, y->ext); break;
        case 3: r = x->size < y->size ? -1 : x->size > y->size ? 1 : 0; break;
        case 4: r = x->mtime < y->mtime ? -1 : x->mtime > y->mtime ? 1 : 0; break;
        case 5: r = x->ctime < y->ctime ? -1 : x->ctime > y->ctime ? 1 : 0; break;
        default: break;
    }
    if (r == 0) r = rubraview_str_natcmp(x->name, y->name);
    if (r == 0) r = a < b ? -1 : a > b ? 1 : 0;   /* stable */
    return g_list_desc ? -r : r;
}

void list_sort(app_state_t *app, int key, bool desc) {
    size_t count = page_count(app);
    if (count < 2 || !list_editable(app)) return;
    app->list_sort_key = key;
    app->list_sort_desc = desc;
    size_t *order = (size_t*)rubraview_malloc_product(count, sizeof(size_t), 1);
    struct list_sort_key *keys = (struct list_sort_key*)rubraview_malloc_product(count, sizeof(*keys), 1);
    if (!order || !keys) { free(order); free(keys); return; }
    rubraview_scratch_t sc;
    proven_arena_t *pa = scratch_begin(app, &sc, 4096u + count * 64u);
    for (size_t i = 0; i < count; ++i) {
        proven_u8str_view_t path = app->source.pages[i].path;
        keys[i] = (struct list_sort_key){ .name = rubraview_path_basename(path), .ext = rubraview_path_ext(path) };
        rubraview_fs_entry_t st;
        if (key >= 3 && rubraview_pal_fs_stat(pa, path, &st)) { keys[i].size = st.size_bytes; keys[i].mtime = st.mtime; keys[i].ctime = st.ctime; }
        order[i] = i;
    }
    scratch_end(app, &sc);
    g_list_keys = keys; g_list_key = key; g_list_desc = desc;
    qsort(order, count, sizeof(size_t), list_sort_cmp);
    g_list_keys = NULL;
    char line[96];
    snprintf(line, sizeof(line), T("the list by %s, %s"), list_sort_word(key), desc ? T("falling") : T("rising"));
    list_apply(app, order, count, NULL, line);
    free(order);
    free(keys);
    app->files_dirty = true;
}

static void chooser_pick(app_state_t *app, int32_t index) {
    chooser_t *c = &app->chooser;
    if (!c->open || index < 0 || (size_t)index >= c->count) return;
    chooser_close(app);
    switch (c->kind) {
        case CHOOSE_TILE: chooser_pick_tile(app, (size_t)index); break;
        case CHOOSE_PANEL: {
            rubraview_panel_t *panel = c->panel == 2 ? &app->sel_panel : &app->panel;
            if ((size_t)c->row >= panel->row_count) break;
            panel->rows[c->row].value = c->values[index];
            if (c->panel == 2) edit_sel_apply_row(app, c->row); else panel_apply_row(app, c->row);
            app->edit_dirty = true;
            break;
        }
        case CHOOSE_SETTING:
            if (!c->def) break;
            rubraview_settings_set(&app->settings, c->def->section, c->def->key, c->values[index]);
            settings_event(app, RUBRAVIEW_SEVENT_CHANGED);
            app->settings_dirty = true;
            break;
        case CHOOSE_SORT: {
            bool desc = rubraview_settings_get(&app->settings, PROVEN_LIT("files"), PROVEN_LIT("picker_sort_desc")) > 0.5;
            picker_sort_set(app, (int)c->values[index], desc);
            break;
        }
        case CHOOSE_ORDER: {
            int key = (int)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("files"), PROVEN_LIT("picker_sort"))) - 1;
            picker_sort_set(app, key < 0 ? 0 : key, c->values[index] > 0.5);
            break;
        }
        case CHOOSE_LISTEXT: {   /* stays open: the next box is a click away */
            int where = c->where;
            rubraview_rect_t f = c->layout.frame;
            list_ext_toggle(app, (int32_t)lround(c->values[index]));
            c->open = true;
            chooser_open_listext(app, where, f.x, f.y, f.y);
            break;
        }
        case CHOOSE_LISTSORT: list_sort(app, (int)lround(c->values[index]), app->list_sort_desc); break;
        case CHOOSE_SHAPE: {   /* RV-192: stays open, its counts new */
            int32_t which = (int32_t)lround(c->values[index]);
            if (which == RUBRAVIEW_SHAPE_UNKNOWN) { c->open = true; break; }
            int where = c->where;
            rubraview_rect_t f = c->layout.frame;
            shape_pick(app, which);
            c->open = true;
            chooser_open_shape(app, where, f.x, f.y, f.y);
            break;
        }
        default: break;
    }
}

/* A press in `where`: the list's when it is open there (a row picks; away closes it). */
bool chooser_press(app_state_t *app, int where, double x, double y) {
    chooser_t *c = &app->chooser;
    if (!c->open) return false;
    if (c->where != where) { chooser_close(app); return false; }
    if (scrollbar_press(app, RVSB_CHOOSER, x, y)) { chooser_dirty(app); return true; }   /* RV-170 */
    int32_t row = rubraview_chooser_row_at(&c->layout, x, y);
    if (row >= 0) chooser_pick(app, row);
    else if (row == -2) chooser_close(app);
    return true;
}

/* RV-170: the list's scroll bar, inside its frame at the right, under its title row. */
bool chooser_sb(app_state_t *app, rubraview_scrollbar_t *sb, double bw, double dpi) {
    const chooser_t *c = &app->chooser;
    if (!c->open) return false;
    const rubraview_chooser_layout_t *l = &c->layout;
    *sb = (rubraview_scrollbar_t){ .area = { l->frame.x + l->frame.width - bw - 3.0 * dpi, l->frame.y + l->row_h + 2.0 * dpi,
                                             bw, l->frame.height - l->row_h - 4.0 * dpi },
                                   .button = bw, .total = (double)l->rows, .shown = (double)l->shown, .first = (double)l->first,
                                   .step = 1.0, .min_thumb = 16.0 * dpi };
    return true;
}

void chooser_sb_to(app_state_t *app, size_t first) {
    rubraview_chooser_layout_t *l = &app->chooser.layout;
    size_t last = l->rows > l->shown ? l->rows - l->shown : 0;
    l->first = first > last ? last : first;
    chooser_dirty(app);
}

rubraview_window_t *chooser_sb_window(app_state_t *app) {
    return app->chooser.open ? chooser_window(app, app->chooser.where) : NULL;
}

bool chooser_key(app_state_t *app, int where, rubraview_key_combo_t combo) {
    chooser_t *c = &app->chooser;
    if (!c->open || c->where != where) return false;
    if (key_is(combo, "Escape")) { chooser_close(app); return true; }
    if (key_is(combo, "Enter")) { chooser_pick(app, c->focus); return true; }
    if (key_is(combo, "Down") || key_is(combo, "Up")) {
        c->focus = rubraview_chooser_step(&c->layout, c->focus, key_is(combo, "Up") ? -1 : 1);
        if (c->focus >= 0 && (size_t)c->focus < c->layout.first) c->layout.first = (size_t)c->focus;
        if (c->focus >= 0 && (size_t)c->focus >= c->layout.first + c->layout.shown) c->layout.first = (size_t)c->focus + 1 - c->layout.shown;
        chooser_dirty(app);
        return true;
    }
    return true;   /* while it is open the other keys wait */
}

bool chooser_wheel(app_state_t *app, int where, double notches) {
    chooser_t *c = &app->chooser;
    if (!c->open || c->where != where) return false;
    rubraview_chooser_scroll(&c->layout, notches > 0.0 ? -1 : 1);
    chooser_dirty(app);
    return true;
}

void draw_chooser(app_state_t *app, rubraview_renderer_t *r, int where, double px, double py) {
    chooser_t *c = &app->chooser;
    if (!c->open || c->where != where || !r) return;
    const rubraview_chooser_layout_t *l = &c->layout;
    rubraview_pal_rect_t f = { l->frame.x, l->frame.y, l->frame.width, l->frame.height };
    int32_t hover = rubraview_chooser_row_at(l, px, py);
    if (hover < 0) hover = c->focus;
    if (where == CHOOSE_IN_SETTINGS) {   /* text, as the settings window writes it */
        double fs = app->settings_font, ch = app->settings_cell_h, cw = app->settings_cell_w;
        rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ f.x - 2.0, f.y - 2.0, f.width + 4.0, f.height + 4.0 }, SETTINGS_ACCENT, 0.0);
        rubraview_pal_render_fill_rect(r, f, SETTINGS_PANE, 0.0);
        rubraview_pal_render_draw_text_mono(r, cstr(c->title), f.x + cw, f.y, fs, SETTINGS_ACCENT);
        for (size_t i = l->first; i < l->first + l->shown && i < c->count; ++i) {
            rubraview_rect_t row = rubraview_chooser_row_rect(l, i);
            if ((int32_t)i == hover) rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ row.x, row.y, row.width, ch }, SETTINGS_FOCUS, 0.0);
            char line[112];
            snprintf(line, sizeof(line), "%s %s", (int32_t)i == c->current ? "\xE2\x97\x8F" : " ", c->words[i]);
            rubraview_pal_render_draw_text_mono(r, cstr(line), row.x + cw, row.y, fs, SETTINGS_TEXT);
        }
        draw_scrollbar(app, r, RVSB_CHOOSER, px, py);   /* RV-170 */
        return;
    }
    double dpi = rubraview_pal_window_dpi_scale(chooser_window(app, where));
    rubraview_pal_render_fill_rect(r, f, 0xF6202020u, 6.0 * dpi);
    rubraview_pal_render_stroke_rect(r, f, COLOR_TILE_CURRENT, 1.5 * dpi, 6.0 * dpi);
    rubraview_pal_render_draw_text(r, cstr(c->title), (rubraview_pal_rect_t){ f.x + 14.0 * dpi, f.y, f.width - 28.0 * dpi, l->row_h },
                                   15.0 * dpi, 0xB0F0F0F0u, RUBRAVIEW_TEXT_LEFT);
    for (size_t i = l->first; i < l->first + l->shown && i < c->count; ++i) {
        rubraview_rect_t row = rubraview_chooser_row_rect(l, i);
        rubraview_pal_rect_t cell = { row.x + 5.0 * dpi, row.y + 3.0 * dpi, row.width - 10.0 * dpi, row.height - 6.0 * dpi };
        if ((int32_t)i == c->current) rubraview_pal_render_fill_rect(r, cell, COLOR_TILE_CURRENT, 4.0 * dpi);
        else if ((int32_t)i == hover) rubraview_pal_render_fill_rect(r, cell, 0x40FFFFFFu, 4.0 * dpi);
        rubraview_pal_rect_t words = { cell.x + 12.0 * dpi, cell.y, cell.width - 20.0 * dpi, cell.height };
        rubraview_pal_render_draw_text(r, cstr(c->words[i]), words, 16.0 * dpi, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
    }
    draw_scrollbar(app, r, RVSB_CHOOSER, px, py);   /* more above or below: where, and RV-170 it moves them */
}

/* 1.25.0: the settings page's scroll bar, at the window's right edge; RV-170: it moves the page. */
bool settings_sb(app_state_t *app, rubraview_scrollbar_t *sb, double bw, double dpi) {
    if (!app->settings_open) return false;
    const rubraview_settings_view_t *v = &app->settings_view;
    int32_t visible = rubraview_settings_view_visible_rows(v);
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->settings_window, &w, &h);
    double ch = app->settings_cell_h;
    *sb = (rubraview_scrollbar_t){ .area = { (double)w - bw - 2.0 * dpi, ch * RUBRAVIEW_SETTINGS_TOP_ROWS, bw, ch * (double)visible },
                                   .button = bw, .total = (double)v->content_height, .shown = (double)visible,
                                   .first = (double)v->scroll, .step = 1.0, .min_thumb = ch };
    return true;
}

void settings_sb_to(app_state_t *app, size_t first) {
    rubraview_settings_view_t *view = &app->settings_view;
    settings_event(app, rubraview_settings_view_scroll(view, (int32_t)first - view->scroll));
    app->settings_dirty = true;
}

void draw_settings_window(app_state_t *app) {
    if (!app->settings_open || !app->settings_dirty) return;
    app->settings_dirty = false;
    rubraview_renderer_t *r = app->settings_renderer;
    const rubraview_settings_view_t *v = &app->settings_view;
    const double cw = app->settings_cell_w, ch = app->settings_cell_h, fs = app->settings_font;
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->settings_window, &w, &h);
    rubraview_settings_sources_t sources = { .user = app, .info = settings_info, .table_row = settings_table_row };
    char line[4096];   /* 1.30.1: a 4K window's rows of 3-byte rules fit */

    rubraview_pal_render_begin(r, SETTINGS_BACKGROUND);

    /* The page list. */
    rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ 0, 0, cw * v->list_cols, (double)h }, SETTINGS_PANE, 0.0);
    rubraview_pal_render_draw_text_mono(r, TU8(" Settings"), 0.0, 0.0, fs, SETTINGS_ACCENT);
    for (int32_t p = 0; p < (int32_t)v->doc->page_count; ++p) {
        double y = ch * (2 + p);
        if (p == v->page) {
            rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ 0, y, cw * v->list_cols, ch }, SETTINGS_FOCUS, 0.0);
        }
        proven_u8str_view_t page = rubraview_settings_page_text(v, p, line, sizeof(line));
        if (p == v->page) settings_text(app, page, 0.0, y, SETTINGS_TEXT);
        else {   /* another page, a click away: a link (owner, 2026-10-18) */
            size_t a = 0;
            while (a < page.size && page.ptr[a] == ' ') ++a;
            settings_text_link(app, (proven_u8str_view_t){ page.ptr + a, page.size - a }, app->settings_cell_w * (double)a, y, SETTINGS_TEXT, page.ptr[a], 0);
        }
    }

    /* The page. Each row's text is kept as drawn, for a marked block to copy. */
    memset(app->settings_grid_len, 0, sizeof(app->settings_grid_len));
    double x0 = cw * v->content_col;
    settings_grid_keep(app, 0, rubraview_tr_u8(rubraview_settings_page_title((size_t)v->page)));
    settings_text(app, rubraview_tr_u8(rubraview_settings_page_title((size_t)v->page)), x0, 0.0, SETTINGS_ACCENT);
    for (size_t i = 0; i < v->line_count; ++i) {
        const rubraview_settings_line_t *ln = &v->lines[i];
        const rubraview_settings_node_t *node = &v->doc->nodes[ln->node];
        for (int32_t k = 0; k < ln->height; ++k) {
            int32_t content_row = ln->row + k - v->scroll;
            if (content_row < 0 || content_row >= rubraview_settings_view_visible_rows(v)) continue;
            double y = ch * (RUBRAVIEW_SETTINGS_TOP_ROWS + content_row);
            if (ln->kind == RUBRAVIEW_LINE_PREVIEW) {
                if (k == 0) {
                    rubraview_pal_rect_t box = { x0, y, cw * (v->cols - v->content_col - 1), ch * ln->height };
                    settings_preview(app, node->name, box);
                }
                continue;
            }
            if ((int32_t)i == v->focus_line && (ln->kind != RUBRAVIEW_LINE_TABLE || k == v->focus_row)) {
                rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ x0 - cw * 0.5, y, cw * (v->cols - v->content_col), ch },
                                               SETTINGS_FOCUS, 0.0);
            }
            proven_u8str_view_t text = rubraview_settings_line_text(v, &app->settings, &sources, i, k, line, sizeof(line));
            uint32_t color = SETTINGS_TEXT;
            if (ln->kind == RUBRAVIEW_LINE_SECTION) color = SETTINGS_ACCENT;
            /* Not read by the viewer yet: dimmed, except under the focus bar, where dim text is unreadable. */
            if (ln->kind == RUBRAVIEW_LINE_SETTING && !v->doc->defs[node->setting].wired &&
                (int32_t)i != v->focus_line) color = SETTINGS_DIM;
            if (ln->kind == RUBRAVIEW_LINE_TABLE && k == 0) color = SETTINGS_ACCENT;
            settings_grid_keep(app, RUBRAVIEW_SETTINGS_TOP_ROWS + content_row, text);
            /* The owner (2026-10-18): whatever a click changes looks like a link — a switch, a choice,
               a slider, an extension's boxes — as the actions and the paths do. */
            const rubraview_setting_def_t *def = ln->kind == RUBRAVIEW_LINE_SETTING ? &v->doc->defs[node->setting] : NULL;
            proven_u8str_view_t value = { text.ptr, 0 };
            if (def) {   /* what follows the label */
                size_t at = 0;
                for (int32_t c = 0; at < text.size && c < v->label_cols;) {
                    uint32_t cp = (unsigned char)text.ptr[at];
                    size_t n = cp < 0x80 ? 1 : cp < 0xE0 ? 2 : cp < 0xF0 ? 3 : 4;
                    c += (int32_t)rubraview_text_columns((proven_u8str_view_t){ text.ptr + at, n });
                    at += n;
                }
                value = (proven_u8str_view_t){ text.ptr + at, text.size - at };
            }
            proven_u8str_view_t label = { text.ptr, (size_t)(value.ptr - text.ptr) };
            double vx = x0 + cw * (double)rubraview_text_columns(label);
            if (ln->kind == RUBRAVIEW_LINE_ACTION) settings_text_link(app, text, x0, y, color, '<', '>');
            else if (def && def->pair != 0) { settings_text(app, label, x0, y, color); settings_text_link(app, value, vx, y, color, '[', 0); }
            else if (def && def->type == RUBRAVIEW_SETTING_PATH) settings_text_link(app, text, x0, y, color, '[', ']');   /* pressed, it is typed or chosen */
            else if (def && def->type == RUBRAVIEW_SETTING_BOOL) { settings_text(app, label, x0, y, color); settings_text_link(app, value, vx, y, color, '[', 0); }
            else if (def && def->type == RUBRAVIEW_SETTING_CHOICE) { settings_text(app, label, x0, y, color); settings_text_link(app, value, vx, y, color, '<', '>'); }
            else if (def) { settings_text(app, label, x0, y, color); settings_text_link(app, value, vx, y, color, '[', ']'); }   /* a slider */
            else if (ln->kind == RUBRAVIEW_LINE_TABLE && k > 0 && settings_row_has_box(text)) settings_text_link(app, text, x0, y, color, '[', 0);
            else settings_text(app, text, x0, y, color);
        }
    }

    skip_dirs_draw_x(app);
    settings_path_draw(app);

    /* Under the page: the last action's result (the buttons are at the top). */
    if (app->settings_message[0]) {
        settings_grid_keep(app, v->rows - 1, cstr(app->settings_message));
        rubraview_pal_render_draw_text_mono(r, cstr(app->settings_message), x0, ch * (v->rows - 1), fs, SETTINGS_DIM);
    }
    /* The marked block, shaded over the text it covers. */
    for (int32_t row = 0; row < v->rows; ++row) {
        int32_t c0 = 0, c1 = 0;
        if (!rubraview_grid_selection_row(&app->settings_sel, row, &c0, &c1)) continue;
        int32_t last_col = v->cols - v->content_col - 1;
        if (c1 > last_col) c1 = last_col;
        if (c1 < c0) continue;
        rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ x0 + cw * c0, ch * row, cw * (c1 - c0 + 1), ch },
                                       0x704A90D9u, 0.0);
    }
    for (int32_t b = 0; b < RUBRAVIEW_BUTTON_COUNT; ++b) {
        int32_t col = 0, width = 0;
        rubraview_settings_view_button_cells(v, (rubraview_settings_button_t)b, &col, &width);
        double y = ch * RUBRAVIEW_SETTINGS_BUTTON_ROW;
        if (b == v->focus_button) {
            rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ cw * col, y, cw * width, ch }, SETTINGS_FOCUS, 0.0);
        }
        settings_text_link(app, rubraview_settings_button_text((rubraview_settings_button_t)b), cw * col, y, SETTINGS_TEXT, '[', ']');
    }

    if (app->settings_tip_shown && app->settings_tip_row > 0 && (size_t)(app->settings_tip_row - 1) < app->keymap.count) {
        /* what the row's action does, under the pointer */
        const rubraview_key_binding_t *b = &app->keymap.bindings[app->settings_tip_row - 1];
        const char *words = rubraview_action_words(b->action);
        if (words) words = rubraview_tr(words);
        char tip[400];
        int n = snprintf(tip, sizeof(tip), "%.*s: %s", (int)b->action.size, b->action.ptr, words ? words : T("(no words for it)"));
        if (n > 0) {
            int32_t ww = 0, wh = 0;
            rubraview_pal_window_get_size(app->settings_window, &ww, &wh);
            double tw = (double)n * cw * 0.95 + cw * 2.0, th = ch * 1.6;
            if (tw > (double)ww - 4.0) tw = (double)ww - 4.0;
            double x = app->settings_tip_x + cw, y = app->settings_tip_y + ch * 1.2;
            if (x + tw > (double)ww) x = (double)ww - tw - 2.0;
            if (y + th > (double)wh) y = app->settings_tip_y - th - ch * 0.4;
            if (x < 0.0) x = 0.0;
            rubraview_pal_rect_t box = { x, y, tw, th };
            rubraview_pal_render_fill_rect(r, box, 0xF0303030u, 3.0);
            rubraview_pal_render_stroke_rect(r, box, COLOR_BOX_BORDER, 1.0, 3.0);
            rubraview_pal_render_draw_text(r, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)tip, .size = (size_t)n < sizeof(tip) ? (size_t)n : sizeof(tip) - 1 },
                                           box, fs, COLOR_TEXT, RUBRAVIEW_TEXT_CENTER);
        }
    }
    if (app->settings_tip_shown && app->settings_tip_row <= 0 && app->settings_tip_help >= 0 &&
        (size_t)app->settings_tip_help < rubraview_settings_document()->def_count) {
        /* A setting's fuller explanation, wrapped, under the pointer. */
        proven_u8str_view_t help = rubraview_tr_u8(rubraview_settings_document()->defs[app->settings_tip_help].help);
        int32_t ww = 0, wh = 0;
        rubraview_pal_window_get_size(app->settings_window, &ww, &wh);
        double tw = cw * 56.0;
        if (tw > (double)ww - 8.0) tw = (double)ww - 8.0;
        double mw = 0.0, mh = 0.0;
        if (!rubraview_pal_render_measure_text_wrapped(r, help, fs, tw - cw * 2.0, &mw, &mh)) mh = ch * 3.0;
        double th = mh + ch;
        double x = app->settings_tip_x + cw, y = app->settings_tip_y + ch * 1.2;
        if (x + tw > (double)ww) x = (double)ww - tw - 2.0;
        if (y + th > (double)wh) y = app->settings_tip_y - th - ch * 0.4;
        if (x < 0.0) x = 0.0;
        if (y < 0.0) y = 0.0;
        rubraview_pal_rect_t box = { x, y, tw, th };
        rubraview_pal_render_fill_rect(r, box, 0xF0303030u, 3.0);
        rubraview_pal_render_stroke_rect(r, box, COLOR_BOX_BORDER, 1.0, 3.0);
        rubraview_pal_render_draw_text(r, help, (rubraview_pal_rect_t){ x + cw, y, tw - cw * 2.0, th }, fs, COLOR_TEXT, RUBRAVIEW_TEXT_LEFT);
    }
    /* 1.25.0 (owner, 2026-10-20: "가로 세로 스크롤바가 생기거나"): a bar at the right while the page is taller; RV-170 */
    draw_scrollbar(app, r, RVSB_SETTINGS, app->settings_px, app->settings_py);
    draw_chooser(app, r, CHOOSE_IN_SETTINGS, -1.0, -1.0);   /* 1.24.0 */
    if (!rubraview_pal_render_end(r)) app->settings_dirty = true;
}
