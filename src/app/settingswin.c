/*
 * The settings window (its file, rows, tables and queue), the folders kept out, a folder typed in, the font picker
 * and the one-of-many list (RV-163, D-197: moved from main.c as it was).
 */
#include "app.h"
#include "rubraview/keyboard.h"
#include "settingslook.h"
#include "settings_keys.h"

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
    film_adjust_load(app);   /* RV-270: the film's values from the start (the Toolbar's caption reads them) */
}

/* RV-311 (owner, 2026-11-19: "단일선택/체크박스/에디트컨트롤/안내메시지 등 뭔가 정형화되고 규격화된 느낌으로 최소한의 노력과
   코드로"): how each kind of text setting is typed, checked and said, in one place; the window keeps no section or key
   names for it. A font is not typed: the font picker chooses it. */
typedef struct text_way {
    bool browse;          /* Browse... and Ctrl+O */
    bool must_be_there;   /* checked on the disk before it is kept */
    bool folder_only;     /* and a folder, not a file */
    const char *hint;     /* said when the box opens */
    const char *kept, *cleared, *left, *missing;
} text_way_t;

static const text_way_t TEXT_WAYS[RUBRAVIEW_TEXT_KIND_COUNT] = {
    [RUBRAVIEW_TEXT_PLAIN]   = { false, false, false, N_("type it; Enter keeps it, Esc leaves it, Ctrl+C copies"),
                                 N_("kept (Delete clears it)"), N_("cleared"), N_("left as it was"), NULL },
    [RUBRAVIEW_TEXT_FOLDER]  = { true, true, true, N_("type or paste a folder; Enter keeps it, Esc leaves it, Ctrl+O browses, Ctrl+C copies"),
                                 N_("folder kept (Delete clears it)"), N_("folder cleared"), N_("folder left as it was"), N_("no such folder - put it right, or Esc") },
    [RUBRAVIEW_TEXT_FILE]    = { true, true, false, N_("type or paste a file or its folder; Enter keeps it, Esc leaves it, Ctrl+O browses"),
                                 N_("kept (Delete clears it)"), N_("cleared"), N_("left as it was"), N_("no such file or folder - put it right, or Esc") },
    [RUBRAVIEW_TEXT_PATTERN] = { false, false, false, N_("type names like *.png;IMG_*; Enter keeps them, Esc leaves it"),
                                 N_("kept (Delete clears it); patterns go with ; between them, as *.png;IMG_*"), N_("cleared"), N_("left as it was"), NULL },
    [RUBRAVIEW_TEXT_FONT]    = { false, false, false, NULL, N_("kept (Delete clears it)"), N_("cleared"), N_("left as it was"), NULL },
    [RUBRAVIEW_TEXT_SEARCH]  = { false, false, false, N_("type the letters to find; Enter keeps them, Esc leaves it as it was"),
                                 N_("kept (Delete clears it)"), N_("cleared"), N_("left as it was"), NULL },
};

static const text_way_t *spath_way(const app_state_t *app) {
    return &TEXT_WAYS[app->spath_kind < RUBRAVIEW_TEXT_KIND_COUNT ? app->spath_kind : RUBRAVIEW_TEXT_PLAIN];
}

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
    if (where.size > 4 && proven_u8str_view_eq_ascii_ci(proven_u8str_view_slice(where, where.size - 4, 4), PROVEN_LIT(".exe"))) where = rubraview_path_dirname(where);
    return where;
}

/* RV-185: G'MIC-Qt's stand-alone download is the one most often taken by mistake; it has no gmic.exe. */
static bool gmic_qt_named(app_state_t *app) {
    proven_u8str_view_t dir = gmic_named_folder(app);
    return dir.size > 0 && rubraview_pal_fs_exists(rubraview_path_join(app->arena, dir, PROVEN_LIT("gmic_qt.exe")));
}



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
                int m = rubraview_fmt(real, sizeof(real), "{}\\Packages\\{}\\LocalCache\\Roaming\\Rubraview\\settings.ini", PROVEN_ARG(local), PROVEN_ARG(app->package_family));
                proven_u8str_view_t candidate = { .ptr = (const proven_byte_t *)real, .size = m > 0 && (size_t)m < sizeof(real) ? (size_t)m : 0 };
                if (candidate.size > 0 && rubraview_pal_fs_exists(candidate)) shown = candidate;
            }
        }
        n = rubraview_fmt(buffer, capacity, "{}{}", PROVEN_ARG(rubraview_bytes_view(shown.ptr, (int)shown.size)),
                     PROVEN_ARG(app->config_mode == RUBRAVIEW_CONFIG_PORTABLE ? T("  (portable)") : ""));
        /* The folder comes from Windows with backslashes and the file name is
           joined with '/': shown with Windows' own separator throughout. */
        for (int i = 0; i < n && (size_t)i < capacity; i++) if (buffer[i] == '/') buffer[i] = '\\';
    } else if (proven_u8str_view_eq(source, PROVEN_LIT("app.others"))) {
        /* D-154: two copies, two menus: what this machine has besides this one. */
        const rubraview_other_copies_t *o = &app->other_copies;
        n = rubraview_fmt(buffer, capacity, "{}",
                     PROVEN_ARG(o->msi && o->store ? T("the MSI's and the Store's too: Windows 11's menu may show twice")
                     : o->store ? T("the Microsoft Store's too: Windows 11's menu is left to it")
                     : o->msi && o->msi_menu && app->packaged ? T("the MSI's too, with its menu: Windows 11's menu shows twice (Remove for every user in that copy)")
                     : o->msi ? T("the one the MSI installed too")
                     : T("none")));
    } else if (proven_u8str_view_eq(source, PROVEN_LIT("app.folder"))) {   /* RV-185: where this copy runs from */
        proven_u8str_view_t dir = program_folder(app);
        n = rubraview_fmt(buffer, capacity, "{}", PROVEN_ARG(rubraview_bytes_view((const char *)dir.ptr, (int)dir.size)));
    } else if (proven_u8str_view_eq(source, PROVEN_LIT("app.kind"))) {
        n = rubraview_fmt(buffer, capacity, "{}",
                     PROVEN_ARG(app->packaged ? T("the Microsoft Store's: Windows has its file types from the package")
                     : app->config_mode == RUBRAVIEW_CONFIG_PORTABLE ? T("portable: the settings beside it")
                     : T("from the zip or the installer: register below")));
    } else if (proven_u8str_view_eq(source, PROVEN_LIT("media.ffmpeg"))) {
        int place = rubraview_pal_ffmpeg_place();
        rubraview_ffmpeg_refusal_t ref = place == 0 ? rubraview_pal_ffmpeg_refused() : RUBRAVIEW_FFMPEG_NOT_REFUSED;
        /* RFC-0009: found where it is named, else beside the program, else where an older Import put it. */
        n = rubraview_fmt(buffer, capacity, "{}",
                     PROVEN_ARG(place == 3 ? T("its DLLs are in the FFmpeg folder")
                     : place == 1 ? T("its DLLs are beside the program")
                     : place == 2 ? T("its DLLs are where an older version's Import put them")
                     : ref == RUBRAVIEW_FFMPEG_OTHER_VERSION ? T("found, but not 9.0 (avcodec-63) - Media Foundation only")
                     : ref == RUBRAVIEW_FFMPEG_NOT_WHOLE ? T("avcodec-63 found, but all five 9.0 DLLs could not be loaded - Media Foundation only")
                     : T("not found (or not 9.x) - Media Foundation only")));
        char encoders[96];   /* RV-139: what it can write pictures with */
        if (place > 0 && n > 0 && (size_t)n < capacity && rubraview_pal_ffmpeg_image_encoders(rubraview_mem_mut(encoders, sizeof(encoders))) && encoders[0]) {
            int m = rubraview_fmt(buffer + n, capacity - (size_t)n, T("; writes AVIF / JPEG XL with {}"), PROVEN_ARG(encoders));
            if (m > 0) n += m;
        }
    } else if (proven_u8str_view_eq(source, PROVEN_LIT("ffmpeg.filters"))) {   /* RFC-0009 D */
        rubraview_ffilter_state_t s = rubraview_pal_ffilter_state();
        if (s == RUBRAVIEW_FFILTER_READY && ffmpeg_list_ready())
            n = rubraview_fmt(buffer, capacity, T("{} filters, under Edit"), PROVEN_ARG((int)g_ffm.filter_count));
        else
            n = rubraview_fmt(buffer, capacity, "{}",
                         PROVEN_ARG(s == RUBRAVIEW_FFILTER_NO_FFMPEG ? T("none: FFmpeg is not found")
                         : s == RUBRAVIEW_FFILTER_OTHER_VERSION ? T("none: its avfilter DLL is not FFmpeg 9.0's")
                         : s == RUBRAVIEW_FFILTER_READY ? T("none: avfilter lists no filter")
                         : T("none: avfilter-12.dll is not beside FFmpeg's other DLLs")));
    } else if (proven_u8str_view_eq(source, PROVEN_LIT("gmic.status"))) {   /* RFC-0009 C */
        if (!g_gmic_exe[0]) n = rubraview_fmt(buffer, capacity, "{}", PROVEN_ARG(!g_gmic_where[0] ? T("not set: name gmic.exe or its folder below")
                                                              : gmic_qt_named(app) ? T("that is G'MIC-Qt's stand-alone: the command-line (CLI) zip holds gmic.exe")
                                                              : T("gmic.exe is not in the place named: the command-line (CLI) zip holds it")));
        else if (g_gmic_job) n = (int)gmic_list_progress(buffer, capacity, app);   /* RV-186 */
        else if (g_gmic.filter_count > 0)
            n = rubraview_fmt(buffer, capacity, T("found; {} filters in {} categories (G'MIC {}), under Edit"), PROVEN_ARG((int)g_gmic.filter_count),
                         PROVEN_ARG((int)g_gmic.category_count), PROVEN_ARG(rubraview_bytes_view((const char *)g_gmic.version.ptr, (int)g_gmic.version.size)));
        else n = rubraview_fmt(buffer, capacity, T("found; {}"), PROVEN_ARG(g_gmic_list_why ? rubraview_tr(g_gmic_list_why) : T("no filter list yet")));
    } else if (proven_u8str_view_eq(source, PROVEN_LIT("gpu.adapter"))) {
        n = rubraview_fmt(buffer, capacity, T("run rubraview --probe-gpu <video> for the details"));
    } else if (proven_u8str_view_eq(source, PROVEN_LIT("memory.use")) && app->mem) {
        /* RFC-0006: the program's memory by use, and the most the session ever held */
        rubraview_mem_stats_t st = rubraview_mem_stats(app->mem);
        n = rubraview_fmt(buffer, capacity, T("session {} MB (most {}), a track {} MB, {} window(s), {} MB for the moment; {} MB held"),
                     PROVEN_ARG((size_t)(st.persist_used >> 20)), PROVEN_ARG((size_t)(st.persist_peak >> 20)), PROVEN_ARG((size_t)(st.media_used >> 20)), PROVEN_ARG((size_t)st.windows_live), PROVEN_ARG((size_t)(st.scratch_live >> 20)),
                     PROVEN_ARG((size_t)(st.total_held >> 20)));
    } else if (proven_u8str_view_eq(source, PROVEN_LIT("cache.films"))) {
        /* RV-216: the network films' copies, how much room they take, where */
        uint64_t bytes = 0;
        size_t copies = rubraview_pal_filmcache_usage(&bytes);
        char folder[1024];
        rubraview_pal_filmcache_folder(folder, sizeof folder);
        n = rubraview_fmt(buffer, capacity, T("{}, {:.1} GB, in {}"), PROVEN_ARG((size_t)copies), PROVEN_ARG((double)bytes / 1073741824.0), PROVEN_ARG(folder[0] ? folder : "-"));
    } else if (proven_u8str_view_eq(source, PROVEN_LIT("cache.used"))) {
        size_t loaded = 0;
        uint64_t bytes = 0;
        for (size_t i = 0; i < page_count(app); ++i) {
            if (!app->pages[i].texture) continue;
            loaded++;
            bytes += (uint64_t)app->pages[i].width * (uint64_t)app->pages[i].height * 4u;
        }
        /* RV-107: and how much that is of the cap. */
        n = rubraview_fmt(buffer, capacity, T("{} page(s), about {} MB of {} MB"), PROVEN_ARG((size_t)loaded),
                     PROVEN_ARG((unsigned long long)((bytes + 524288u) / 1048576u)),
                     PROVEN_ARG((size_t)((app->cache_budget > 0 ? app->cache_budget : PAGE_CACHE_BUDGET) / 1048576u)));
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
        proven_u8str_view_t key = { .ptr = (const proven_byte_t *)OLD[i], .size = proven_cstr_len(OLD[i]) };
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
        int w = index == 0 ? rubraview_fmt(buffer, capacity, T("Folder  (Enter changes it in the box below; Delete or the x at the end takes it off)"))
              : index <= n ? rubraview_fmt(buffer, capacity, "{}{}{}", PROVEN_ARG(changing ? "> " : ""), PROVEN_ARG(rubraview_bytes_view(dirs[index - 1].ptr, (int)dirs[index - 1].size)),
                                      PROVEN_ARG(changing ? T("   (being changed below)") : ""))
              : index == n + 1 ? (typing ? 0
                                  : rubraview_fmt(buffer, capacity, T(" type or paste a folder to add  (Enter or a click; Ctrl+O browses)")))
              : 0;
        return w > 0 ? (size_t)w < capacity ? (size_t)w : capacity - 1 : 0;
    }
    if (proven_u8str_view_eq(source, PROVEN_LIT("gmic_filters"))) {   /* RV-186: the list G'MIC made, a row a category */
        int w = 0;
        if (index == 0) w = rubraview_fmt(buffer, capacity, "{}", PROVEN_ARG(T("Category (filters)  -  the first of them")));
        else if (g_gmic.category_count == 0) w = index == 1 ? rubraview_fmt(buffer, capacity, "{}", PROVEN_ARG(T("no list yet: Make the filter list above"))) : 0;
        else if ((int32_t)index <= g_gmic.category_count) {
            int32_t c = (int32_t)index - 1, count = 0;
            for (int32_t i = 0; i < g_gmic.filter_count; ++i) count += g_gmic.filters[i].category == c;
            w = rubraview_fmt(buffer, capacity, "{} ({})  - ", PROVEN_ARG(rubraview_bytes_view((const char *)g_gmic.categories[c].ptr, (int)g_gmic.categories[c].size)), PROVEN_ARG((int)count));
            bool first = true;
            for (int32_t i = 0; i < g_gmic.filter_count && w > 0 && (size_t)w + 8 < capacity; ++i) {
                if (g_gmic.filters[i].category != c) continue;
                int m = rubraview_fmt(buffer + w, capacity - (size_t)w, "{}{}", PROVEN_ARG(first ? " " : ", "), PROVEN_ARG(rubraview_bytes_view((const char *)g_gmic.filters[i].name.ptr, (int)g_gmic.filters[i].name.size)));
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
        if (index == 0) w = rubraview_fmt(buffer, capacity, "{}", PROVEN_ARG(T("Plug-in  (version, licence)  -  file")));
        else if (!on) w = index == 1 ? rubraview_fmt(buffer, capacity, "{}", PROVEN_ARG(T("plug-ins are off"))) : 0;
        else if (g_plugin_count == 0) w = index == 1 ? rubraview_fmt(buffer, capacity, T("none in {}"), PROVEN_ARG(g_plugin_folder)) : 0;
        else if (index <= g_plugin_count) {
            const rubraview_plugin_t *p = &g_plugins[index - 1];
            w = p->usable ? rubraview_fmt(buffer, capacity, "{}  ({}, {})  -  {}", PROVEN_ARG(p->name), PROVEN_ARG(p->version[0] ? p->version : "?"),
                                     PROVEN_ARG(p->licence[0] ? p->licence : T("licence not said")), PROVEN_ARG(p->file))
                          : rubraview_fmt(buffer, capacity, T("{}  -  not used: {}"), PROVEN_ARG(p->file), PROVEN_ARG(p->why ? rubraview_tr(p->why) : "?"));
        }
        return w > 0 ? (size_t)w < capacity ? (size_t)w : capacity - 1 : 0;
    }
    if (!proven_u8str_view_eq(source, PROVEN_LIT("keymap"))) return 0;
    return keys_table_row(app, index, buffer, capacity);   /* RV-310: settings_keys.c */
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

static int32_t settings_sidebar_visible(const app_state_t *app, int32_t h) {
    double avail = (double)h - app->settings_cell_h * 2.5;
    int32_t vis = app->settings_cell_h > 0.0 && avail > 0.0 ? (int32_t)(avail / app->settings_cell_h) : 0;
    return vis > 0 ? vis : 0;
}

static void settings_sidebar_clamp(app_state_t *app, int32_t h) {
    int32_t pages = (int32_t)(app->settings_view.doc ? app->settings_view.doc->page_count : RUBRAVIEW_TAB_COUNT);
    int32_t vis = settings_sidebar_visible(app, h);
    int32_t max_s = pages > vis ? pages - vis : 0;
    if (app->settings_page_scroll > max_s) app->settings_page_scroll = max_s;
    if (app->settings_page_scroll < 0) app->settings_page_scroll = 0;
}

static void settings_sidebar_reveal(app_state_t *app, int32_t h) {
    int32_t p = app->settings_view.page, vis = settings_sidebar_visible(app, h);
    if (vis <= 0) { app->settings_page_scroll = p; return; }
    if (p < app->settings_page_scroll) app->settings_page_scroll = p;
    if (p >= app->settings_page_scroll + vis) app->settings_page_scroll = p - vis + 1;
    settings_sidebar_clamp(app, h);
}

static void settings_layout_buttons(app_state_t *app, int32_t w, int32_t cols) {
    const rubraview_settings_view_t *v = &app->settings_view;
    double cw = app->settings_cell_w, ch = app->settings_cell_h;
    if (cw <= 0.0 || ch <= 0.0) { app->settings_content_top = ch * (double)RUBRAVIEW_SETTINGS_TOP_ROWS; return; }
    int32_t list_cols = v->doc ? v->list_cols : (cols / 3 < 16 ? cols / 3 : 16);
    if (v->doc == NULL && list_cols < 8) list_cols = 8;
    int32_t content_col = v->doc ? v->content_col : list_cols + 2;
    double x0 = cw * (double)content_col;
    double right = (double)w - cw;
    double room = right - x0;
    if (room < cw * 12.0) room = cw * 12.0;
    double bw[RUBRAVIEW_BUTTON_COUNT] = { 0 }, bx[RUBRAVIEW_BUTTON_COUNT] = { 0 };
    uint32_t brow[RUBRAVIEW_BUTTON_COUNT] = { 0 };
    for (int32_t b = 0; b < RUBRAVIEW_BUTTON_COUNT; ++b) {
        int32_t cells = rubraview_settings_text_cells(rubraview_settings_button_text((rubraview_settings_button_t)b));
        bw[b] = cw * (double)cells;
        if (bw[b] > room) bw[b] = room;
    }
    double dpi = rubraview_pal_window_dpi_scale(app->settings_window);
    double bh = ch, gap_x = cw * 0.7, gap_y = 2.0 * dpi;
    size_t rows = rubraview_flow_inline(bw, RUBRAVIEW_BUTTON_COUNT, gap_x, room, brow, bx);
    double by0 = rows > 1 ? ch * 0.95 : ch;
    for (int32_t b = 0; b < RUBRAVIEW_BUTTON_COUNT; ++b)
        app->settings_btn[b] = (rubraview_pal_rect_t){ x0 + bx[b], by0 + (double)brow[b] * (bh + gap_y), bw[b], bh };
    double block = rows ? (double)rows * bh + (double)(rows - 1) * gap_y : 0.0;
    double top = by0 + block + 4.0 * dpi;
    if (top < ch * (double)RUBRAVIEW_SETTINGS_TOP_ROWS) top = ch * (double)RUBRAVIEW_SETTINGS_TOP_ROWS;
    app->settings_content_top = top;
}

double settings_row_y(const app_state_t *app, int32_t row) {
    const rubraview_settings_view_t *v = &app->settings_view;
    double ch = app->settings_cell_h;
    if (row <= 0) return 0.0;
    if (row >= v->rows - 1) {
        int32_t w = 0, h = 0;
        rubraview_pal_window_get_size(app->settings_window, &w, &h);
        double my = (double)h - ch;
        return my > app->settings_content_top ? my : app->settings_content_top;
    }
    if (row < RUBRAVIEW_SETTINGS_TOP_ROWS) return ch * (double)row;
    return app->settings_content_top + ch * (double)(row - RUBRAVIEW_SETTINGS_TOP_ROWS);
}

static int32_t settings_y_to_row(const app_state_t *app, double y) {
    if (y < app->settings_cell_h) return 0;
    if (y < app->settings_content_top) return RUBRAVIEW_SETTINGS_BUTTON_ROW;
    int32_t r = RUBRAVIEW_SETTINGS_TOP_ROWS + (int32_t)((y - app->settings_content_top) / app->settings_cell_h);
    int32_t max_r = app->settings_view.rows - 2;
    if (r > max_r) r = max_r;
    return r;
}

static void settings_grid(app_state_t *app, int32_t *out_cols, int32_t *out_rows) {
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->settings_window, &w, &h);
    int32_t cols = (int32_t)((double)w / app->settings_cell_w);
    if (cols < 26) cols = 26;
    settings_layout_buttons(app, w, cols);
    double avail_px = (double)h - app->settings_content_top - app->settings_cell_h;
    int32_t avail_rows = app->settings_cell_h > 0.0 && avail_px > 0.0 ? (int32_t)(avail_px / app->settings_cell_h) : 1;
    if (avail_rows < 1) avail_rows = 1;
    *out_cols = cols;
    *out_rows = RUBRAVIEW_SETTINGS_TOP_ROWS + avail_rows + 1;
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
    /* Converge content_top & visible row count once real content_col is known. */
    settings_grid(app, &cols, &rows);
    rubraview_settings_view_resize(&app->settings_view, cols, rows);
    app->settings_page_scroll = 0;
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
    if (choose_mode_on()) return;   /* RV-314: a choosing window writes none of the viewer's files */
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
        proven_sys_mem_copy(mine.revision, app->settings.revision, sizeof(mine.revision));
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
    app->spath_view.dragging = false;
    rubraview_card_close(&app->spath_card);
    if (app->settings_window) rubraview_pal_window_set_cursor_shape(app->settings_window, RUBRAVIEW_CURSOR_ARROW);
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
static char g_lang_file[1024]; /* RV-289: its file, for a file job's windows ("" English) */
const char *lang_file_in_use(void) { return g_lang_file; }
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
    if (rubraview_cstr_cmp(want, "auto") == 0) {
        char tag[96];
        want = rubraview_pal_process_ui_language(tag, sizeof(tag)) ? rubraview_lang_pick(tag, LANG_PACKS, 5) : NULL;
    }
    if (want && rubraview_cstr_cmp(want, "en") == 0) want = NULL;
    if (rubraview_cstr_cmp(want ? want : "", g_lang_code) == 0) return;

    rubraview_lang_use(NULL);
    rubraview_lang_free(&g_lang);
    g_lang_code[0] = '\0';
    g_lang_file[0] = '\0';
    if (want) {
        char name[48];
        rubraview_fmt(name, sizeof(name), "lang/{}.txt", PROVEN_ARG(want));
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
            if (loaded) rubraview_fmt(g_lang_file, sizeof g_lang_file, "{}", PROVEN_ARG(path));
            scratch_end(app, &sc);
        }
        if (loaded) {
            rubraview_lang_use(&g_lang);
            rubraview_fmt(g_lang_code, sizeof(g_lang_code), "{}", PROVEN_ARG(want));
        } else {
            char line[96];
            int n = rubraview_fmt(line, sizeof(line), T("no language pack lang\\{}.txt beside rubraview.exe: English it is"), PROVEN_ARG(want));
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
/* ---- RV-311: applying the settings by table ----
   Most settings are read where they are used and need nothing here. A setting whose change has to do something at
   once (lay the pages out again, list the folder again, load the keymap ...) is a row: the settings it watches and
   the function that applies them, called the first time and whenever their values' fingerprint changes
   (rubraview_settings_print: a key, a click, Revert or another window's file alike). What is copied into the program
   on every pass (it also follows the screen's scale) is ap_every_pass. */

static void ap_keys_tables(app_state_t *app, bool first) {   /* RV-193, RV-303, RFC-0010 §4.8 */
    (void)first;
    if (g_keys_by_key || g_keys_by_row) keymap_table_refresh(app);
}

static void ap_fit(app_state_t *app, bool first) {
    static const rubraview_fit_mode_t FIT[] = { RUBRAVIEW_FIT_WINDOW, RUBRAVIEW_FIT_WIDTH, RUBRAVIEW_FIT_HEIGHT,
                                                RUBRAVIEW_FIT_ACTUAL_SIZE, RUBRAVIEW_FIT_SMART, RUBRAVIEW_FIT_STRETCH };
    int f = (int)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("viewer"), PROVEN_LIT("fit_mode")));
    app->fit_mode = FIT[f >= 0 && f < 6 ? f : 0];
    if (!first) reset_view(app);
}

static void ap_layout(app_state_t *app, bool first) {
    /* The one place the layout is set from: the settings window, the toolbox's tile, the menu and the key all write
       this setting, so they cannot disagree (owner, 2026-10-09). The setting's order is rubraview_layout_choice_t's,
       and the way the pages run comes with it (owner, 2026-10-11). Comic is one page at a time with a wide scan as its
       two halves; every other layout shows a wide scan whole. */
    int l = (int)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("viewer"), PROVEN_LIT("layout")));
    app->layout_opts.mode = rubraview_layout_choice_mode(l);
    app->layout_opts.direction = rubraview_layout_choice_direction(l);
    app->layout_opts.auto_split_wide_spreads = app->layout_opts.mode == RUBRAVIEW_PAGE_LAYOUT_COMIC;
    app->needs_relayout = true;
    app->strip_offset = 0.0;
    app->strip_base_w = 0.0;
    if (!first) reset_view(app);
}

static void ap_spreads(app_state_t *app, bool first) {
    (void)first;
    rubraview_settings_t *st = &app->settings;
    app->spread_detect = rubraview_settings_get(st, PROVEN_LIT("viewer"), PROVEN_LIT("wide_alone")) > 0.5;   /* RV-284 */
    app->layout_opts.spread_ar_threshold = app->spread_detect ? 1.15 : 1.0e9;
    app->layout_opts.first_alone = rubraview_settings_get(st, PROVEN_LIT("viewer"), PROVEN_LIT("first_page_alone")) > 0.5;   /* RV-224 */
    app->layout_opts.portrait_collapse_ar = rubraview_settings_get(st, PROVEN_LIT("viewer"), PROVEN_LIT("portrait_collapse")) > 0.5 ? 1.0 : 0.0;
    app->needs_relayout = true;
}

static void ap_pixel_grid(app_state_t *app, bool first) {
    (void)first;
    app->pixel_grid = rubraview_settings_get(&app->settings, PROVEN_LIT("viewer"), PROVEN_LIT("pixel_grid")) > 0.5;
}

static void ap_subtitle_face(app_state_t *app, bool first) {
    (void)app; (void)first;
    g_subtitle_style++;   /* the subtitles' face drawn again */
}

static void ap_listing(app_state_t *app, bool first) {   /* RV-106, RV-169: listed again, on the same file */
    if (!first) folder_relist(app);
}

static void ap_audio_device(app_state_t *app, bool first) {   /* RV-237: a change moves what plays */
    (void)first;
    proven_u8str_view_t dev = rubraview_settings_get_text(&app->settings, PROVEN_LIT("audio"), PROVEN_LIT("output_device"));
    char now[256] = "";
    if (dev.size < sizeof(now)) { proven_sys_mem_copy(now, dev.ptr, dev.size); now[dev.size] = '\0'; }
    rubraview_pal_audio_use_device(now);
}

static void ap_settings_look(app_state_t *app, bool first) {   /* 1.25.0: this window's face, size or line spacing */
    if (first || !app->settings_open || !app->settings_window) return;
    settings_measure(app);
    int32_t cols = 0, rows = 0;
    settings_grid(app, &cols, &rows);
    rubraview_settings_view_resize(&app->settings_view, cols, rows);
    app->settings_dirty = true;
}

static void ap_text_on(app_state_t *app, bool first) {   /* 1.25.0: text files shown at all */
    bool on = rubraview_settings_get(&app->settings, PROVEN_LIT("text"), PROVEN_LIT("viewer")) > 0.5;
    if (on == g_text_on && !first) return;
    g_text_on = on;
    if (!first) settings_say(app, on ? T("text files are listed and shown again")
                                     : T("text files are no longer listed or shown; a text file Windows sends here is not shown either"));
}

static void ap_color_management(app_state_t *app, bool first) {
    rubraview_pal_image_set_color_management(rubraview_settings_get(&app->settings, PROVEN_LIT("display"), PROVEN_LIT("color_management")) > 0.5);
    if (!first) unload_all_pages(app);   /* decoded again, with or without the profile */
}

static void ap_export(app_state_t *app, bool first) {
    (void)first;
    app->export_options.privacy_clean = rubraview_settings_get(&app->settings, PROVEN_LIT("cache"), PROVEN_LIT("privacy_clean")) > 0.5;
    app->export_options.keep_details = (int32_t)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("cache"), PROVEN_LIT("keep_details")));   /* 1.17.0 */
}

static void ap_keymap_file(app_state_t *app, bool first) {
    if (!first) load_keymap(app);
}

static void ap_decoder_retry(app_state_t *app, bool first) {
    if (first || app->media || app->picker_open) return;
    int32_t cur = current_page_index(app);
    if (cur >= 0 && (size_t)cur < page_count(app) && app->pages[cur].failed &&
        is_media_path(app->source.pages[cur].path)) {
        app->pages[cur].failed = false;
        app->media_skip_pending = false;
        app->media_no_ffmpeg_page = 0;
        media_prepare(app);
    }
}

static void ap_picker_sort(app_state_t *app, bool first) {
    if (!first && app->picker_open) picker_resort(app);
}

typedef struct apply_row {
    const char *keys;   /* the settings it watches, "section.key ..." */
    void (*run)(app_state_t *app, bool first);
} apply_row_t;

static const apply_row_t APPLY[] = {
    { "keys.key_order keys.row_screen keys.row_mods keys.row_find", ap_keys_tables },
    { "viewer.fit_mode", ap_fit },
    { "viewer.layout", ap_layout },
    { "viewer.wide_alone viewer.first_page_alone viewer.portrait_collapse", ap_spreads },
    { "viewer.pixel_grid", ap_pixel_grid },
    { "video.subtitle_font", ap_subtitle_face },
    { "files.pattern files.sort_mode files.sort_ascending files.subfolders files.show files.same_kind", ap_listing },
    { "files.picker_sort files.picker_sort_desc files.picker_folders_first", ap_picker_sort },
    { "audio.output_device", ap_audio_device },
    { "general.font_settings general.size_settings general.settings_spacing", ap_settings_look },
    { "text.viewer", ap_text_on },
    { "display.color_management", ap_color_management },
    { "cache.privacy_clean cache.keep_details", ap_export },
    { "keys.use_keymap_file", ap_keymap_file },
    { "video.decoder video.ffmpeg_dir video.ffmpeg_isolated", ap_decoder_retry },
};

/* What the program keeps a copy of, every pass (several follow the screen's scale too). */
static void ap_every_pass(app_state_t *app) {
    rubraview_settings_t *st = &app->settings;
    double dpi = rubraview_pal_window_dpi_scale(app->window);
    lang_apply(app);
    app->frameless = rubraview_settings_get(st, PROVEN_LIT("general"), PROVEN_LIT("frameless")) > 0.5;
    rubraview_pal_window_set_frameless(app->window, app->frameless);
    app->titlebar.trigger_zone = fmax(36.0, rubraview_settings_get(st, PROVEN_LIT("general"), PROVEN_LIT("titlebar_trigger_px"))) * dpi;   /* never less than the bar (owner, 2026-10-14) */
    app->titlebar.hide_delay = rubraview_settings_get(st, PROVEN_LIT("general"), PROVEN_LIT("titlebar_hide_ms")) / 1000.0;
    g_gutter = rubraview_settings_get(st, PROVEN_LIT("viewer"), PROVEN_LIT("gutter_px")) * dpi;
    g_zoom_step = 1.0 + rubraview_settings_get(st, PROVEN_LIT("viewer"), PROVEN_LIT("zoom_step_percent")) / 100.0;
    g_whole_zoom = rubraview_settings_get(st, PROVEN_LIT("viewer"), PROVEN_LIT("whole_zoom")) > 0.5;
    static const uint32_t BACK[] = { 0xFF101010u, 0xFF000000u, 0xFF606060u, 0xFFFFFFFFu };
    int back = (int)lround(rubraview_settings_get(st, PROVEN_LIT("viewer"), PROVEN_LIT("background")));
    g_canvas = BACK[back >= 0 && back < 4 ? back : 0];
    static const uint32_t INK[] = { 0xFFFFFFFFu, 0xFFFFE94Du, 0xFF66E0FFu, 0xFF7CF08Au, 0xFFFF9AD0u };
    int ink = (int)lround(rubraview_settings_get(st, PROVEN_LIT("video"), PROVEN_LIT("subtitle_color")));
    g_subtitle_argb = INK[ink >= 0 && ink < 5 ? ink : 0];
    /* RV-176 (D-208): FFmpeg in a process of its own, or inside, before anything asks for it; its folder before anything
       asks for FFmpeg, and again on a change (while it is not loaded, the new folder is tried at the next ask) */
    rubraview_pal_ffmpeg_set_isolated(rubraview_settings_get(st, PROVEN_LIT("video"), PROVEN_LIT("ffmpeg_isolated")) > 0.5);
    rubraview_pal_ffmpeg_set_folders(imported_folder(app, "ffmpeg"), rubraview_pal_fs_expand_env(app->arena,
        rubraview_path_typed_folder(rubraview_settings_get_text(st, PROVEN_LIT("video"), PROVEN_LIT("ffmpeg_dir")))));
    int cp = (int)lround(rubraview_settings_get(st, PROVEN_LIT("files"), PROVEN_LIT("archive_codepage")));
    app->archive_codepage = cp >= 0 && cp <= (int)RUBRAVIEW_CODEPAGE_WESTERN ? (rubraview_codepage_t)cp : RUBRAVIEW_CODEPAGE_AUTO;
    rubraview_pal_audio_set_latency_ms((uint32_t)lround(rubraview_settings_get(st, PROVEN_LIT("audio"), PROVEN_LIT("wasapi_latency_ms"))));
    picker_columns_read(rubraview_settings_get_text(st, PROVEN_LIT("files"), PROVEN_LIT("picker_columns")));   /* 1.25.0 */
    static const double TILE[] = { 48.0, 64.0, 96.0 };
    int tile = (int)lround(rubraview_settings_get(st, PROVEN_LIT("display"), PROVEN_LIT("tile_base_px")));
    g_tile_base = TILE[tile >= 0 && tile < 3 ? tile : 1];
    g_widget_scale = rubraview_box_widget_size_clamp(rubraview_settings_get(st, PROVEN_LIT("ui"), PROVEN_LIT("widget_size"))) / 100.0;   /* RV-178 */
    static const uint32_t ACCENT[] = { 0xFFC0304Au, 0xFF3A78D8u, 0xFF2E9E6Au, 0xFFD08A1Eu, 0xFF1E9E9Eu, 0xFF8A4FD0u };
    int accent = (int)lround(rubraview_settings_get(st, PROVEN_LIT("display"), PROVEN_LIT("accent")));
    g_accent = ACCENT[accent >= 0 && accent < 6 ? accent : 0];
    app->cache_budget = (size_t)rubraview_settings_get(st, PROVEN_LIT("cache"), PROVEN_LIT("memory_cap_mb")) * 1024u * 1024u;
    app->precache.want_ahead = app->precache.lookahead = (size_t)lround(rubraview_settings_get(st, PROVEN_LIT("cache"), PROVEN_LIT("lookahead")));
    app->precache.want_behind = app->precache.lookbehind = (size_t)lround(rubraview_settings_get(st, PROVEN_LIT("cache"), PROVEN_LIT("lookbehind")));
    filmcache_for(app, (proven_u8str_view_t){ 0 });   /* RV-216: the cache's settings, till a film is opened */
}

static void settings_apply_rest(app_state_t *app) {
    static bool first = true;
    static uint64_t seen[sizeof APPLY / sizeof APPLY[0]];
    ap_every_pass(app);
    for (size_t i = 0; i < sizeof APPLY / sizeof APPLY[0]; ++i) {
        uint64_t now = rubraview_settings_print(&app->settings, APPLY[i].keys);
        if (!first && now == seen[i]) continue;
        seen[i] = now;
        APPLY[i].run(app, first);
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
    if (rubraview_settings_get(&app->settings, PROVEN_LIT("viewer"), PROVEN_LIT("thumb_bar")) <= 0.5) app->filmstrip.visible = false;   /* RFC-0010 §5 */
    app_log_limit(app);   /* RV-256 */
    {   /* RFC-0007: plug-ins on or off, or another folder: looked for again */
        size_t before = g_plugin_count;
        plugins_from_settings(app);
        if (app->settings_window && g_plugin_count != before) skip_dirs_table_rows(app);
    }
    gmic_from_settings(app);   /* RFC-0009 C */
    int dec = (int)lround(rubraview_settings_get(&app->settings, PROVEN_LIT("video"), PROVEN_LIT("decoder")));
    app->media_decoder = (dec >= 0 && dec <= (int)RUBRAVIEW_DECODER_FFMPEG_ONLY)
        ? (rubraview_decoder_mode_t)dec : RUBRAVIEW_DECODER_WINDOWS_FFMPEG;
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
static void keys_file_chosen(app_state_t *app, void *ctx, bool accepted, const proven_u8str_view_t *paths, size_t count, uint32_t filter) {
    bool writing = ctx != NULL;
    rubraview_file_dialog_result_t picked = { .paths = (proven_u8str_view_t *)paths, .count = count, .accepted = accepted };
    (void)filter;
    if (!picked.accepted || picked.count == 0) return;
    if (!app->settings_open) return;   /* the window closed meanwhile */
    if (writing) {
        proven_u8str_view_t text = rubraview_keymap_serialize(app->arena, &app->keymap);
        settings_say(app, text.size > 0 && rubraview_pal_fs_write_file(picked.paths[0], text)
                              ? T("keys exported") : T("could not write that file"));
        return;
    }
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

static void keys_export_or_import(app_state_t *app, bool writing) {   /* RV-314: the file chosen in Rubraview's own window */
    static const rubraview_file_filter_t FILTERS[] = { { "Keymap (*.ini)", "*.ini" }, { N_("All files"), "*.*" } };
    rubraview_file_filter_t f[2] = { FILTERS[0], { T("All files"), "*.*" } };
    app->key_capture = 0;
    choose_opts_t o = { .mode = writing ? RUBRAVIEW_CHOOSE_SAVE : RUBRAVIEW_CHOOSE_FILE, .title = writing ? T("Export keys") : T("Import keys"),
                        .filters = f, .filter_count = 2, .name = writing ? PROVEN_LIT("keymap") : PROVEN_LIT(""),
                        .over = rubraview_pal_window_native_handle(app->settings_window) };
    (void)choose_ask(app, &o, keys_file_chosen, writing ? (void *)1 : NULL);
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

static void lang_import_chosen(app_state_t *app, void *ctx, bool accepted, const proven_u8str_view_t *paths, size_t n_paths, uint32_t filter) {
    (void)ctx;
    rubraview_file_dialog_result_t picked = { .paths = (proven_u8str_view_t *)paths, .count = n_paths, .accepted = accepted };
    (void)filter;
    if (!picked.accepted || picked.count == 0) return;
    if (!app->settings_open) return;
    proven_u8str_view_t from[16], names[16];
    size_t count = 0;
    for (size_t i = 0; i < picked.count && count < 16; i++) {
        /* Only a pack this build can choose, under its own name (ko.txt), and one that reads. */
        proven_u8str_view_t stem = rubraview_path_stem(picked.paths[i]);
        const char *code = NULL;
        for (size_t k = 0; k < 5 && !code; k++) {
            size_t at = 0, n = proven_cstr_len(LANG_PACKS[k]);
            while (at < n && at < stem.size && rubraview_ascii_tolower((unsigned char)stem.ptr[at]) == rubraview_ascii_tolower((unsigned char)LANG_PACKS[k][at])) at++;
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
        rubraview_fmt(name, sizeof(name), "{}.txt", PROVEN_ARG(code));
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
    rubraview_fmt(g_lang_code, sizeof(g_lang_code), "?");   /* read again, the one in use too */
    lang_apply(app);
    settings_say(app, where == 0 ? T("the language packs could not be copied")
                      : count < picked.count ? T("language packs imported; files that are not one were left out")
                      : T("language packs imported"));
    app->settings_dirty = true;
}

static void lang_import(app_state_t *app) {   /* RV-314: the packs chosen in Rubraview's own window */
    const rubraview_file_filter_t FILTERS[] = { { T("Language pack (*.txt)"), "*.txt" } };
    choose_opts_t o = { .mode = RUBRAVIEW_CHOOSE_FILES, .title = T("Import language packs"), .filters = FILTERS, .filter_count = 1,
                        .over = rubraview_pal_window_native_handle(app->settings_window) };
    (void)choose_ask(app, &o, lang_import_chosen, NULL);
}

void settings_say(app_state_t *app, const char *text) {
    rubraview_fmt(app->settings_message, sizeof(app->settings_message), "{}", PROVEN_ARG(text));
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
        { "m3u8", rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("ext_m3u8")) > 0.5, rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("menu_m3u8")) > 0.5 },   /* RV-254 */
        { "m3u", rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("ext_m3u")) > 0.5, rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("menu_m3u")) > 0.5 },   /* RV-254 */
        { "pls", rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("ext_pls")) > 0.5, rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("menu_pls")) > 0.5 },   /* RV-254 */
        { "rvlist", rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("ext_rvlist")) > 0.5, rubraview_settings_get(st, PROVEN_LIT("shell"), PROVEN_LIT("menu_rvlist")) > 0.5 },   /* RV-254 */
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
        if (!rubraview_pal_shell_unregister_for(rubraview_shell_extensions_all(), all_users)) ok = false;
        proven_u8str_view_t list = types == SHELL_PART_APPLY ? rubraview_shell_selection_of_kinds(arena, selection, RUBRAVIEW_SHELL_FILE_TYPES)
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
    bool ok = rubraview_pal_shell_unregister_for(rubraview_shell_extensions_all(), all_users);
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

    char words[512], menu_words[512];   /* RV-254: the playlists' too */
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
        int n = adding ? rubraview_fmt(args, sizeof(args), "--register-shell --all-users --types={}{}{}{}{}",
                                  PROVEN_ARG(words_len > 0 ? words : menu_words), PROVEN_ARG(menu_len > 0 ? " --menu-types=" : ""), PROVEN_ARG(menu_len > 0 ? menu_words : ""),
                                  PROVEN_ARG(types ? "" : " --remove-types"), PROVEN_ARG(menu ? "" : " --remove-menu"))
                       : rubraview_fmt(args, sizeof(args), "--unregister-shell --all-users");
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
        rubraview_fmt(say, sizeof(say), T("could not {} {}"), PROVEN_ARG(adding ? T("register") : T("remove")), PROVEN_ARG(who));
    } else if (!adding) {
        rubraview_fmt(say, sizeof(say), T("removed {}"), PROVEN_ARG(who));
    } else if (!menu) {
        rubraview_fmt(say, sizeof(say), T("registered {}: file types"), PROVEN_ARG(who));
    } else if (!all) {
        rubraview_fmt(say, sizeof(say), T("registered {}; the menu is under \"Show more options\" (Windows 11's own menu: for every user)"), PROVEN_ARG(who));
    } else if (code == SHELL_DONE_NO_PARTS) {
        rubraview_fmt(say, sizeof(say), T("registered {}; Windows 11's menu needs rubraview_menu.dll and .msix beside the program (they are in the zip)"), PROVEN_ARG(who));
    } else if (code == SHELL_DONE_REFUSED) {
        rubraview_fmt(say, sizeof(say), T("registered {}; Windows did not take the Windows 11 menu's package"), PROVEN_ARG(who));
    } else if (code == SHELL_DONE_STORE) {
        rubraview_fmt(say, sizeof(say), T("registered {}; Windows 11's menu is left to the Store's copy (this one's is under \"Show more options\")"), PROVEN_ARG(who));
    } else {
        rubraview_fmt(say, sizeof(say), T("registered {}, with Windows 11's menu"), PROVEN_ARG(who));
    }
    settings_say(app, say);
}

static void settings_path_end(app_state_t *app);

void settings_event(app_state_t *app, rubraview_settings_event_t event) {
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
            if (app->spath_active) settings_path_end(app);
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
            if (app->spath_active) settings_path_end(app);
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
            if (app->spath_active) settings_path_end(app);
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
                    rubraview_textedit_set(&app->spath_ec.te, (size_t)(row - 1) < have ? dirs[row - 1] : PROVEN_LIT(""));
                    rubraview_textedit_select_all(&app->spath_ec.te);
                    /* Typed in the box that is always there, the list's last row, never over another row. */
                    rubraview_settings_view_focus_table_row(&app->settings_view, cstr(FOLDER_LISTS[folders].table), (int32_t)n + 1);
                    if ((size_t)row <= n) settings_say(app, T("change the folder in the box; Enter keeps it, Esc leaves it, Ctrl+O browses"));
                }
                break;
            }
            if (g_keys_by_key) { settings_say(app, T("the keys by key are to read: Show by function to change them")); break; }
            if (g_keys_by_row && row >= 1 && (size_t)row <= g_key_row_count && !g_key_rows[row - 1].heading) {   /* RV-303 */
                const rubraview_keyrow_entry_t *e = &g_key_rows[row - 1];
                char text[200], one[64];
                proven_u8str_view_t named = rubraview_key_combo_format(one, sizeof(one), e->combo);
                if (e->binding < 0) {
                    rubraview_fmt(text, sizeof(text), T("{} is not set: Show by function, then Enter on an action, gives it a key"), PROVEN_ARG(named));
                } else if (event == RUBRAVIEW_SEVENT_TABLE_EDIT) {
                    app->key_capture = row;
                    rubraview_fmt(text, sizeof(text), T("press the key {} should use instead of {} (Esc: leave it as it is)"),
                                  PROVEN_ARG(app->keymap.bindings[e->binding].action), PROVEN_ARG(named));
                } else {
                    proven_u8str_view_t action = app->keymap.bindings[e->binding].action;
                    rubraview_keymap_unbind_combo(app->arena, &app->keymap, (size_t)e->binding, e->combo);
                    rubraview_fmt(text, sizeof(text), T("took {} off {} (Revert puts it back)"), PROVEN_ARG(named), PROVEN_ARG(action));
                    keymap_table_refresh(app);
                }
                settings_say(app, text);
                break;
            }
            int32_t at = keymap_row_binding(row);
            if (at < 0) break;   /* a context's heading */
            const rubraview_key_binding_t *b = &app->keymap.bindings[at];
            char text[160];
            if (event == RUBRAVIEW_SEVENT_TABLE_EDIT) {
                app->key_capture = row;
                rubraview_fmt(text, sizeof(text), T("press the key to add to {} (Esc: leave it as it is)"),
                         PROVEN_ARG(rubraview_bytes_view(b->action.ptr, (int)b->action.size)));
            } else if (b->combo_count == 0) {
                rubraview_fmt(text, sizeof(text), T("{} has no key to take off"), PROVEN_ARG(rubraview_bytes_view(b->action.ptr, (int)b->action.size)));
            } else {
                char one[64];
                proven_u8str_view_t last = rubraview_key_combo_format(one, sizeof(one), b->combos[b->combo_count - 1]);
                rubraview_keymap_unbind_last(&app->keymap, (size_t)at);
                rubraview_fmt(text, sizeof(text), T("took {} off {} (Revert puts it back)"),
                         PROVEN_ARG(rubraview_bytes_view(last.ptr, (int)last.size)), PROVEN_ARG(rubraview_bytes_view(b->action.ptr, (int)b->action.size)));
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
                settings_say(app, rubraview_tr(TEXT_WAYS[def->text_kind < RUBRAVIEW_TEXT_KIND_COUNT ? def->text_kind : 0].cleared));
                break;
            }
            /* RV-136 (owner, 2026-10-18): a font is chosen in the program's own picker; RV-311: every `font` (the places' faces
               were taken as folders before) */
            if (def->text_kind == RUBRAVIEW_TEXT_FONT) {
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
            } else if (proven_u8str_view_eq(node->name, PROVEN_LIT("keys.layout"))) {   /* RV-217 */
                g_keys_layout = !g_keys_layout;
                g_keys_by_row = false;
                app->key_capture = 0;
                keymap_table_refresh(app);
                settings_say(app, g_keys_layout ? T("the keyboard: red alone, yellow Shift, blue Ctrl, green Alt, purple two or more")
                                                : g_keys_by_key ? T("the keys by key: what each does, and where") : T("the keys by function, a context at a time"));
            } else if (proven_u8str_view_eq(node->name, PROVEN_LIT("keys.by_key"))) {   /* RV-184 */
                g_keys_layout = false;
                g_keys_by_row = false;
                g_keys_by_key = !g_keys_by_key;
                app->key_capture = 0;
                keymap_table_refresh(app);
                settings_say(app, g_keys_by_key ? T("the keys by key: what each does, and where") : T("the keys by function, a context at a time"));
            } else if (proven_u8str_view_eq(node->name, PROVEN_LIT("keys.by_row"))) {   /* RV-303 (RFC-0010 §4.8) */
                g_keys_layout = false;
                g_keys_by_key = false;
                g_keys_by_row = !g_keys_by_row;
                app->key_capture = 0;
                keymap_table_refresh(app);
                settings_say(app, g_keys_by_row ? T("the keys by keyboard row: a row, then what each of its keys does on the screen chosen")
                                                : T("the keys by function, a context at a time"));
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
                if (named.size > 0 && named.size < sizeof(folder)) { proven_sys_mem_copy(folder, named.ptr, named.size); folder[named.size] = '\0'; }
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
                    app->picker_thumbs = (struct picker_thumb *)rubraview_alloc_zero(app->picker_listing.count ? app->picker_listing.count : 1,
                                                                       sizeof(*app->picker_thumbs));
                    app->picker_thumb_count = app->picker_thumbs ? app->picker_listing.count : 0;
                }
                settings_say(app, T("the picker's thumbnails were let go"));
            } else if (proven_u8str_view_eq(node->name, PROVEN_LIT("cache.clear_films"))) {
                /* RV-216: every copy not playing now */
                uint64_t freed = rubraview_pal_filmcache_clear();
                char said[96];
                rubraview_fmt(said, sizeof said, T("the network films' copies were deleted: {:.1} GB"), PROVEN_ARG((double)freed / 1073741824.0));
                settings_say(app, said);
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
    int32_t row = app->key_capture;
    int32_t at = keymap_row_binding(row);
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
    if (g_keys_by_row) {
        /* RV-303 (RFC-0010 §4.8): the key of the row chosen is given up for the one pressed; refused, it stays */
        rubraview_key_combo_t old = g_key_rows[row - 1].combo;
        char was[64];
        proven_u8str_view_t old_name = rubraview_key_combo_format(was, sizeof(was), old);
        rubraview_keymap_unbind_combo(app->arena, &app->keymap, index, old);
        rubraview_bind_result_t got = rubraview_keymap_bind(app->arena, &app->keymap, index, combo, &holder);
        proven_u8str_view_t action = app->keymap.bindings[index].action;
        if (got == RUBRAVIEW_BIND_ADDED || got == RUBRAVIEW_BIND_ALREADY)
            rubraview_fmt(text, sizeof(text), T("{}: {} instead of {}"), PROVEN_ARG(action), PROVEN_ARG(name), PROVEN_ARG(old_name));
        else {
            rubraview_keymap_bind(app->arena, &app->keymap, index, old, NULL);
            if (got == RUBRAVIEW_BIND_TAKEN && holder)
                rubraview_fmt(text, sizeof(text), T("not changed: {} is {}'s key{}{}{}"), PROVEN_ARG(name), PROVEN_ARG(holder->action),
                              PROVEN_ARG(holder->context.size ? " (" : ""), PROVEN_ARG(holder->context), PROVEN_ARG(holder->context.size ? ")" : ""));
            else rubraview_fmt(text, sizeof(text), T("could not add {}"), PROVEN_ARG(name));
        }
        keymap_table_refresh(app);
        settings_say(app, text);
        return;
    }
    rubraview_bind_result_t result = rubraview_keymap_bind(app->arena, &app->keymap, index, combo, &holder);
    const rubraview_key_binding_t *b = &app->keymap.bindings[index];
    switch (result) {
        case RUBRAVIEW_BIND_ADDED:
            rubraview_fmt(text, sizeof(text), T("{} added to {}"), PROVEN_ARG(rubraview_bytes_view(name.ptr, (int)name.size)), PROVEN_ARG(rubraview_bytes_view(b->action.ptr, (int)b->action.size)));
            break;
        case RUBRAVIEW_BIND_ALREADY:
            rubraview_fmt(text, sizeof(text), T("{} already has {}"), PROVEN_ARG(rubraview_bytes_view(b->action.ptr, (int)b->action.size)), PROVEN_ARG(rubraview_bytes_view(name.ptr, (int)name.size)));
            break;
        case RUBRAVIEW_BIND_TAKEN:
            rubraview_fmt(text, sizeof(text), T("not changed: {} is {}'s key{}{}{}"), PROVEN_ARG(rubraview_bytes_view(name.ptr, (int)name.size)),
                     PROVEN_ARG(rubraview_bytes_view(holder->action.ptr, (int)holder->action.size)),
                     PROVEN_ARG(holder->context.size ? " (" : ""), PROVEN_ARG(rubraview_bytes_view(holder->context.ptr, (int)holder->context.size)),
                     PROVEN_ARG(holder->context.size ? ")" : ""));
            break;
        case RUBRAVIEW_BIND_FAILED:
        default:
            rubraview_fmt(text, sizeof(text), T("could not add {}"), PROVEN_ARG(rubraview_bytes_view(name.ptr, (int)name.size)));
            break;
    }
    settings_say(app, text);
}

/* Everything the settings window's queue holds; returns how many. */
/* ---- A folder typed or pasted in the settings window (owner, 2026-09-30:
   "경로 직접 입력 가능하게. 반대로 현재 경로를 편집 가능한 에디트 컨트롤에
   넣어서 복사해갈 수도 있게") ---- */

/* RFC-0010 §4.8: the keys' search is letters, no folder: no Browse, nothing looked for on the disk */

void settings_path_begin(app_state_t *app, const rubraview_setting_def_t *def) {
    editbox_begin(&app->spath_ec, app->spath_buffer, sizeof(app->spath_buffer), false);
    proven_u8str_view_t now = rubraview_settings_get_text(&app->settings, def->section, def->key);
    rubraview_textedit_set(&app->spath_ec.te, now.size < sizeof(app->spath_buffer) ? now : PROVEN_LIT(""));
    rubraview_textedit_select_all(&app->spath_ec.te);
    app->spath_view = (editbox_view_t){0};
    rubraview_card_close(&app->spath_card);
    app->spath_blink_on = false;
    app->spath_number = false;
    app->spath_section = def->section;
    app->spath_key = def->key;
    app->spath_kind = def->text_kind;
    app->spath_before = app_keep(app, now);
    app->spath_list_index = -1;
    app->spath_composing_length = 0;
    app->spath_active = true;
    rubraview_pal_window_text_input(app->settings_window, true);
    if (spath_way(app)->hint) settings_say(app, rubraview_tr(spath_way(app)->hint));
}

/* 1.24.0 (owner, 2026-10-19: "모든 슬라이드 컨트롤은 텍스트로 직접 입력"): a slider's number typed in the same box. */
void settings_number_begin(app_state_t *app) {
    const rubraview_setting_def_t *def = rubraview_settings_view_focused_def(&app->settings_view);
    if (!def || (def->type != RUBRAVIEW_SETTING_INT && def->type != RUBRAVIEW_SETTING_FLOAT)) return;
    char now[32];
    double v = rubraview_settings_get(&app->settings, def->section, def->key);
    int n = (int)proven_cstr_len(rubraview_setting_number_text(def, v, now, sizeof now));   /* RV-311: with its decimals */
    editbox_begin(&app->spath_ec, app->spath_buffer, sizeof(app->spath_buffer), false);
    rubraview_textedit_set(&app->spath_ec.te, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)now, .size = n > 0 ? (size_t)n : 0 });
    rubraview_textedit_select_all(&app->spath_ec.te);
    app->spath_view = (editbox_view_t){0};
    rubraview_card_close(&app->spath_card);
    app->spath_blink_on = false;
    app->spath_section = def->section;
    app->spath_key = def->key;
    app->spath_kind = RUBRAVIEW_TEXT_PLAIN;
    app->spath_list_index = -1;
    app->spath_composing_length = 0;
    app->spath_number = true;
    app->spath_active = true;
    rubraview_pal_window_text_input(app->settings_window, true);
    char line[160];
    char lo[32], hi[32];
    rubraview_fmt(line, sizeof(line), T("type a number, {} to {}; Enter keeps it, Esc leaves it"), PROVEN_ARG(rubraview_setting_number_text(def, def->min_value, lo, sizeof lo)),
                  PROVEN_ARG(rubraview_setting_number_text(def, def->max_value, hi, sizeof hi)));
    settings_say(app, line);
}

static void settings_path_end(app_state_t *app) {
    app->spath_number = false;
    app->spath_btn_n = 0;
    app->spath_active = false;
    app->spath_view.dragging = false;
    rubraview_card_close(&app->spath_card);
    if (app->settings_window) rubraview_pal_window_set_cursor_shape(app->settings_window, RUBRAVIEW_CURSOR_ARROW);
    app->spath_list_index = -1;
    app->spath_composing_length = 0;
    rubraview_pal_window_text_input(app->settings_window, false);
    app->settings_dirty = true;
}

static void settings_path_commit(app_state_t *app) {
    proven_u8str_view_t text = rubraview_textedit_text(&app->spath_ec.te);
    if (app->spath_number) {   /* 1.24.0; RV-311: an int's and a float's typed number read one way (core) */
        const rubraview_setting_def_t *def = rubraview_settings_find(app->spath_section, app->spath_key);
        double v = 0.0;
        rubraview_number_typed_t got = rubraview_setting_number_typed(def, text, &v);
        if (got == RUBRAVIEW_NUMBER_NOT) { settings_say(app, T("that is not a number - put it right, or Esc")); return; }
        rubraview_settings_set(&app->settings, app->spath_section, app->spath_key, v);
        settings_path_end(app);
        settings_event(app, RUBRAVIEW_SEVENT_CHANGED);
        if (got == RUBRAVIEW_NUMBER_CLAMPED) {   /* said, not cut without a word */
            char line[160];
            char a[32], lo[32], hi[32];
            (void)rubraview_fmt(line, sizeof line, T("kept as {}: it goes from {} to {}"), PROVEN_ARG(rubraview_setting_number_text(def, v, a, sizeof a)),
                                PROVEN_ARG(rubraview_setting_number_text(def, def->min_value, lo, sizeof lo)), PROVEN_ARG(rubraview_setting_number_text(def, def->max_value, hi, sizeof hi)));
            settings_say(app, line);
        }
        return;
    }
    /* RV-311: what is typed is checked as its kind says (a folder list's row is a folder) */
    const text_way_t *way = app->spath_list_index >= 0 ? &TEXT_WAYS[RUBRAVIEW_TEXT_FOLDER] : spath_way(app);
    /* Explorer's "Copy as path" puts quotes round it; spaces at the ends are no part of a folder (a command keeps its own) */
    while (way->must_be_there && text.size > 0 && (text.ptr[0] == ' ' || text.ptr[0] == '"')) { text.ptr++; text.size--; }
    while (way->must_be_there && text.size > 0 && (text.ptr[text.size - 1] == ' ' || text.ptr[text.size - 1] == '"')) text.size--;
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
        settings_say(app, rubraview_tr(way->cleared));
        return;
    }
    /* %LOCALAPPDATA%\ffmpeg is a folder too: looked for as Windows will read it. */
    rubraview_fs_entry_t found;
    if (way->must_be_there && (!rubraview_pal_fs_stat(app->arena, rubraview_pal_fs_expand_env(app->arena, text), &found) ||
                               (way->folder_only && !found.is_directory))) {
        settings_say(app, rubraview_tr(way->missing));
        return;
    }
    /* The settings borrow their text: a copy, not the box's own buffer,
       which the next folder typed would write over (VM, 2026-10-01: Folder
       1 turned into Folder 2's path once Folder 2 was typed). */
    rubraview_settings_set_text(&app->settings, app->spath_section, app->spath_key, app_keep(app, text));
    settings_path_end(app);
    settings_took_effect(app);
    settings_say(app, rubraview_tr(way->kept));
}

static char g_spath_for[160];   /* RV-314: the setting whose box asked, so a late answer finds the right one */

static void settings_path_chosen(app_state_t *app, void *ctx, bool accepted, const proven_u8str_view_t *paths, size_t count, uint32_t filter) {
    (void)ctx; (void)filter;
    char now[160];
    (void)rubraview_fmt(now, sizeof now, "{}.{}", PROVEN_ARG(app->spath_section), PROVEN_ARG(app->spath_key));
    if (!accepted || count == 0 || !app->spath_active || rubraview_cstr_cmp(now, g_spath_for) != 0 || paths[0].size >= sizeof(app->spath_buffer)) return;
    rubraview_textedit_set(&app->spath_ec.te, paths[0]);   /* in the box, for Enter to keep */
    app->settings_dirty = true;
}

static void settings_path_browse(app_state_t *app) {
    bool folder = spath_way(app)->folder_only;   /* RV-311: a file's kind picks a file */
    proven_u8str_view_t typed = rubraview_textedit_text(&app->spath_ec.te);
    rubraview_fs_entry_t st;
    proven_u8str_view_t start = typed.size && rubraview_pal_fs_stat(app->arena, typed, &st) ? (st.is_directory ? typed : rubraview_path_dirname(typed)) : PROVEN_LIT("");
    (void)rubraview_fmt(g_spath_for, sizeof g_spath_for, "{}.{}", PROVEN_ARG(app->spath_section), PROVEN_ARG(app->spath_key));
    choose_opts_t o = { .mode = folder ? RUBRAVIEW_CHOOSE_FOLDER : RUBRAVIEW_CHOOSE_FILE, .title = folder ? T("Choose a folder") : T("Choose a file"),
                        .start_dir = start, .over = rubraview_pal_window_native_handle(app->settings_window) };   /* RV-314 */
    (void)choose_ask(app, &o, settings_path_chosen, NULL);
}

/* Keys while the box is open: true when it took the key. */
static bool settings_path_key(app_state_t *app, rubraview_key_combo_t combo) {
    if (!app->spath_active) return false;
    bool ctrl = (combo.modifiers & RUBRAVIEW_MOD_CTRL) != 0;
    if (key_is(combo, "Enter")) {
        editbox_text(&app->spath_ec, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)app->spath_composing, .size = app->spath_composing_length }, true);
        app->spath_composing_length = 0;
        settings_path_commit(app);
    } else if (key_is(combo, "Escape")) {
        if (!app->spath_number && app->spath_kind == RUBRAVIEW_TEXT_SEARCH)   /* what it followed while typed, put back */
            rubraview_settings_set_text(&app->settings, app->spath_section, app->spath_key, app->spath_before);
        settings_path_end(app);
        settings_say(app, rubraview_tr(spath_way(app)->left));
    } else if (ctrl && key_is(combo, "O") && spath_way(app)->browse) {
        settings_path_browse(app);
    } else {
        (void)editbox_key(app->settings_window, &app->spath_ec, combo, false);   /* RV-293 */
        if (!app->spath_number && app->spath_kind == RUBRAVIEW_TEXT_SEARCH)   /* RV-311: a search follows the letters as they are typed */
            rubraview_settings_set_text(&app->settings, app->spath_section, app->spath_key, app_keep(app, rubraview_textedit_text(&app->spath_ec.te)));
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

/* A press in the inline settings edit bar: buttons, field caret/drag/editmenu, move grip & resize edges.
   Returns true when handled inside the bar; clicks outside unfocus the bar modelessly and return false. */
static bool settings_path_press(app_state_t *app, double x, double y, int button, uint32_t modifiers) {
    if (!app->spath_active) return false;
    if (button == RUBRAVIEW_MOUSE_LEFT) {
        for (int32_t i = 0; i < app->spath_btn_n; ++i) {
            if (!pal_rect_has(app->spath_btn[i], x, y)) continue;
            switch (app->spath_btn_kind[i]) {
                case RENAME_BTN_OK:
                    editbox_text(&app->spath_ec, (proven_u8str_view_t){ .ptr = (const proven_byte_t *)app->spath_composing, .size = app->spath_composing_length }, true);
                    app->spath_composing_length = 0;
                    settings_path_commit(app);
                    break;
                case RENAME_BTN_CANCEL:
                    if (!app->spath_number && app->spath_kind == RUBRAVIEW_TEXT_SEARCH)
                        rubraview_settings_set_text(&app->settings, app->spath_section, app->spath_key, app->spath_before);
                    settings_path_end(app);
                    settings_say(app, rubraview_tr(spath_way(app)->left));
                    break;
                case RENAME_BTN_BROWSE:
                    settings_path_browse(app);
                    break;
                default: break;
            }
            app->settings_dirty = true;
            return true;
        }
    }
    if (app->spath_view.box.width > 0.0 && pal_rect_has(app->spath_view.box, x, y)) {
        app->spath_card.focused = true;
        rubraview_pal_window_text_input(app->settings_window, true);
        app->spath_composing_length = 0;
        if (button == RUBRAVIEW_MOUSE_RIGHT) {
            chooser_open_editmenu_in(app, CHOOSE_IN_SETTINGS, x, y);
            app->settings_dirty = true;
            return true;
        }
        if (button == RUBRAVIEW_MOUSE_LEFT) {
            (void)editbox_pointer_down(app->settings_renderer, &app->spath_ec, &app->spath_view,
                                       x, y, (modifiers & RUBRAVIEW_MOD_SHIFT) != 0);
            app->settings_dirty = true;
            return true;
        }
    }
    if (app->spath_card.open && button == RUBRAVIEW_MOUSE_LEFT) {
        bool was = app->spath_card.focused;
        bool on = rubraview_card_press(&app->spath_card, x, y);
        if (app->spath_card.focused != was) rubraview_pal_window_text_input(app->settings_window, app->spath_card.focused);
        app->settings_dirty = true;
        return on;
    }
    return false;
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
    int32_t mc = (int32_t)(app->settings_tip_x / cw), mr = settings_y_to_row(app, app->settings_tip_y);
    for (int32_t l = 0; l < FOLDER_LIST_COUNT; ++l)
    for (size_t k = 1, n = folder_list_count(app, l); k <= n; ++k) {
        int32_t row = rubraview_settings_view_table_screen_row(v, cstr(FOLDER_LISTS[l].table), (int32_t)k);
        if (row < 0) continue;
        double ry = settings_row_y(app, row);
        rubraview_pal_rect_t cell = { cw * skip_dirs_x_col(app), ry, cw * 3, ch };
        bool over = mr == row && mc >= skip_dirs_x_col(app) && mc < skip_dirs_x_col(app) + 3;
        rubraview_pal_render_fill_rect(r, cell, over ? COLOR_CLOSE_HOVER : SETTINGS_PANE, 2.0);
        rubraview_pal_render_draw_text(r, PROVEN_LIT("X"), cell, app->settings_font, SETTINGS_TEXT, RUBRAVIEW_TEXT_CENTER);
    }
}

/* Waiting frames under folder lists awaiting a typed folder. */
static void skip_dirs_draw_frames(app_state_t *app) {
    rubraview_renderer_t *r = app->settings_renderer;
    const double cw = app->settings_cell_w, ch = app->settings_cell_h;
    int32_t row = 0, col0 = 0, cols = 0;
    int32_t typing = app->spath_active ? (app->spath_list_index >= 0 ? app->spath_list : -1) : -2;
    for (int32_t l = 0; l < FOLDER_LIST_COUNT; ++l) {   /* waiting: each field's frame round its hint */
        if (l == typing || !settings_path_box(app, l, &row, &col0, &cols)) continue;
        rubraview_pal_render_stroke_rect(r, (rubraview_pal_rect_t){ cw * col0, settings_row_y(app, row), cw * cols, ch }, 0xFF707070u, 1.0, 0.0);
    }
}

/* Movable & resizable inline rectangular edit bar over the settings window. */
static void settings_path_draw(app_state_t *app) {
    if (!app->spath_active) return;
    rubraview_renderer_t *r = app->settings_renderer;
    const rubraview_settings_view_t *v = &app->settings_view;
    const double cw = app->settings_cell_w, ch = app->settings_cell_h, fs = app->settings_font;
    double dpi = rubraview_pal_window_dpi_scale(app->settings_window);
    int32_t ww = 0, wh = 0;
    rubraview_pal_window_get_size(app->settings_window, &ww, &wh);
    if (ww <= 0 || wh <= 0) return;

    double pad = 5.0 * dpi, grip_w = 14.0 * dpi, grip_gap = 4.0 * dpi, btn_gap = 4.0 * dpi;
    double min_w = 220.0 * dpi, min_h = fmax(34.0 * dpi, ch + 10.0 * dpi);
    if (!app->spath_card.open) {
        int32_t row = 0, col0 = 0, cols = 0;
        int32_t typing = app->spath_list_index >= 0 ? app->spath_list : -1;
        rubraview_rect_t want;
        if (settings_path_box(app, typing, &row, &col0, &cols)) {
            double ry = settings_row_y(app, row);
            double want_h = min_h;
            double want_y = ry + (ch - want_h) * 0.5;
            want = (rubraview_rect_t){ cw * (double)col0 - pad - grip_w - grip_gap, want_y,
                                       fmax(min_w, cw * (double)cols + pad + grip_w + grip_gap + 92.0 * dpi), want_h };
        } else {
            double avail_w = fmax(min_w, (double)ww - cw * (double)v->content_col - cw);
            want = (rubraview_rect_t){ cw * (double)v->content_col, app->settings_content_top + ch, fmin(avail_w, 520.0 * dpi), min_h };
        }
        app->spath_card.margin = 4.0 * dpi;
        app->spath_card.edge = 5.0 * dpi;
        app->spath_card.title_height = 0.0;
        app->spath_card.grip_width = grip_w;
        rubraview_card_open_at(&app->spath_card, (double)ww, (double)wh, want, min_w, min_h);
    }
    rubraview_card_fit(&app->spath_card, (double)ww, (double)wh, min_w, min_h);
    bool focused = app->spath_card.focused;
    rubraview_pal_rect_t card = { app->spath_card.rect.x, app->spath_card.rect.y, app->spath_card.rect.width, app->spath_card.rect.height };
    double round = 2.0 * dpi;
    rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ card.x + 3.0 * dpi, card.y + 5.0 * dpi, card.width, card.height }, 0x60000000u, round);
    rubraview_pal_render_fill_rect(r, card, 0xFA242424u, round);
    rubraview_pal_render_stroke_rect(r, card, focused ? 0xFF5B9BD5u : COLOR_BOX_BORDER, (focused ? 1.5 : 1.0) * dpi, round);

    rubraview_rect_t row_r = { card.x + pad, card.y + pad, fmax(1.0, card.width - 2.0 * pad), fmax(1.0, card.height - 2.0 * pad) };
    int32_t kinds[3] = {0};
    uint32_t icons[3] = {0};
    const char *fbs[3] = {0};
    int styles[3] = {0};
    double btn_w[3] = {0};
    int32_t n = 0;
    double bw = fmin(fmax(row_r.height, 26.0 * dpi), 40.0 * dpi);
    if (!app->spath_number && (app->spath_list_index >= 0 || spath_way(app)->browse)) {
        kinds[n] = RENAME_BTN_BROWSE; icons[n] = RUBRAVIEW_ICON_BROWSE; fbs[n] = "..."; styles[n] = 0; btn_w[n] = bw; n++;
    }
    kinds[n] = RENAME_BTN_OK; icons[n] = RUBRAVIEW_ICON_CHECK; fbs[n] = "\xE2\x9C\x93"; styles[n] = 1; btn_w[n] = bw; n++;
    kinds[n] = RENAME_BTN_CANCEL; icons[n] = RUBRAVIEW_ICON_CLOSE; fbs[n] = "\xC3\x97"; styles[n] = 2; btn_w[n] = bw; n++;

    rubraview_editbar_layout_t lo = rubraview_editbar_layout(row_r, grip_w, grip_gap, btn_w, n, btn_gap, 80.0 * dpi);
    rubraview_pal_rect_t grip = { lo.grip.x, lo.grip.y, lo.grip.width, lo.grip.height };
    draw_editbar_grip(r, grip, pal_rect_has(grip, app->settings_px, app->settings_py), app->spath_card.drag == RUBRAVIEW_CARD_TITLE, dpi);

    rubraview_pal_rect_t field = { lo.field.x, lo.field.y, lo.field.width, lo.field.height };
    rubraview_pal_render_fill_rect(r, field, 0xFF101010u, round);
    rubraview_pal_render_stroke_rect(r, field, focused ? 0xFF5B9BD5u : COLOR_BOX_BORDER, 1.0, round);

    app->spath_view.box = field;
    app->spath_view.size = fs;
    editbox_draw(r, &app->spath_ec, &app->spath_view,
                 (proven_u8str_view_t){ .ptr = (const proven_byte_t *)app->spath_composing, .size = app->spath_composing_length },
                 focused, SETTINGS_TEXT, dpi);

    app->spath_btn_n = lo.btn_count;
    for (int32_t i = 0; i < lo.btn_count; ++i) {
        rubraview_pal_rect_t b = { lo.btns[i].x, lo.btns[i].y, lo.btns[i].width, lo.btns[i].height };
        app->spath_btn[i] = b;
        app->spath_btn_kind[i] = kinds[i];
        bool over = pal_rect_has(b, app->settings_px, app->settings_py);
        draw_editbar_button(r, b, styles[i], icons[i], fbs[i], NULL, over, false, dpi);
    }
}

size_t settings_pump(app_state_t *app) {
    size_t handled = 0;
    rubraview_window_event_t event;
    if (app->spath_active && app->spath_card.open && app->spath_card.focused) {
        bool blink = fmod(rubraview_pal_time_now_seconds(), 1.0) < 0.5;
        if (blink != app->spath_blink_on) { app->spath_blink_on = blink; app->settings_dirty = true; }
    }
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
                app->settings_tip_shown = false;
                double prev_top = app->settings_content_top;
                int32_t prev_col = view->content_col;
                int32_t cols = 0, rows = 0;
                settings_grid(app, &cols, &rows);
                rubraview_settings_view_resize(view, cols, rows);
                if (view->content_col != prev_col || fabs(app->settings_content_top - prev_top) > 0.5) {
                    settings_grid(app, &cols, &rows);
                    rubraview_settings_view_resize(view, cols, rows);
                }
                settings_sidebar_reveal(app, event.resize.height);
                app->settings_dirty = true;
                break;
            }
            case RUBRAVIEW_WINDOW_EVENT_DPI_CHANGED: {
                settings_measure(app);
                int32_t cols = 0, rows = 0, sw = 0, sh = 0;
                settings_grid(app, &cols, &rows);
                rubraview_settings_view_resize(view, cols, rows);
                settings_grid(app, &cols, &rows);
                rubraview_settings_view_resize(view, cols, rows);
                rubraview_pal_window_get_size(app->settings_window, &sw, &sh);
                settings_sidebar_reveal(app, sh);
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
                if (app->spath_active) {
                    if (app->spath_card.open && !app->spath_card.focused) {
                        if (key_is(event.key.combo, "Escape")) {
                            if (!app->spath_number && app->spath_kind == RUBRAVIEW_TEXT_SEARCH)
                                rubraview_settings_set_text(&app->settings, app->spath_section, app->spath_key, app->spath_before);
                            settings_path_end(app);
                            settings_say(app, rubraview_tr(spath_way(app)->left));
                            break;
                        }
                    } else if (settings_path_key(app, event.key.combo)) {
                        break;
                    }
                }
                if (settings_copy_key(app, event.key.combo)) break;
                rubraview_settings_key_t key;
                if (settings_key_of(event.key.combo, &key)) {
                    int32_t sw = 0, sh = 0;
                    int32_t before_page = view->page;
                    rubraview_pal_window_get_size(app->settings_window, &sw, &sh);
                    settings_event(app, rubraview_settings_view_key(view, &app->settings, key));
                    if (app->spath_active && view->page != before_page) settings_path_end(app);
                    settings_sidebar_reveal(app, sh);
                }
                break;
            }
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_DOWN: {
                int32_t sw = 0, sh = 0;
                rubraview_pal_window_get_size(app->settings_window, &sw, &sh);
                if (chooser_press(app, CHOOSE_IN_SETTINGS, event.mouse.x, event.mouse.y)) break;   /* 1.24.0 */
                if (settings_path_press(app, event.mouse.x, event.mouse.y, event.mouse.button, event.mouse.modifiers)) break;
                /* RV-170: the scroll bar: its thumb dragged, its track a page, its buttons a row */
                if (event.mouse.button == RUBRAVIEW_MOUSE_LEFT && scrollbar_press(app, RVSB_SETTINGS, event.mouse.x, event.mouse.y)) {
                    app->settings_dirty = true;
                    break;
                }
                if (event.mouse.button != RUBRAVIEW_MOUSE_LEFT) break;
                if (app->key_capture > 0) {
                    app->key_capture = 0;   /* a click elsewhere is a change of mind */
                    settings_say(app, T("no key added"));
                }
                /* Top action buttons laid out in wrapping rows. */
                if (event.mouse.x >= app->settings_cell_w * view->list_cols) {
                    bool hit_top = false;
                    static const rubraview_settings_event_t EV[RUBRAVIEW_BUTTON_COUNT] = {
                        RUBRAVIEW_SEVENT_REVERT, RUBRAVIEW_SEVENT_DEFAULTS,
                        RUBRAVIEW_SEVENT_DEFAULTS_PAGE, RUBRAVIEW_SEVENT_DEFAULTS_ONE,
                        RUBRAVIEW_SEVENT_CLOSE,
                    };
                    for (int32_t b = 0; b < RUBRAVIEW_BUTTON_COUNT; ++b) {
                        if (!pal_rect_has(app->settings_btn[b], event.mouse.x, event.mouse.y)) continue;
                        view->focus_line = -1;
                        view->focus_button = b;
                        settings_event(app, EV[b]);
                        hit_top = true;
                        break;
                    }
                    if (hit_top) break;
                }
                /* Left page sidebar (scrolled when window is short). */
                if (event.mouse.x < app->settings_cell_w * view->list_cols) {
                    int32_t p = app->settings_page_scroll + (int32_t)(event.mouse.y / app->settings_cell_h) - 2;
                    int32_t vis = settings_sidebar_visible(app, sh);
                    if (p >= app->settings_page_scroll && p < app->settings_page_scroll + vis &&
                        view->doc && p >= 0 && p < (int32_t)view->doc->page_count) {
                        if (app->spath_active && p != view->page) settings_path_end(app);
                        rubraview_settings_view_set_page(view, p);
                        settings_sidebar_reveal(app, sh);
                        app->settings_dirty = true;
                    }
                    break;
                }
                if (event.mouse.y < app->settings_content_top || event.mouse.y >= (double)sh - app->settings_cell_h) break;
                /* A drag marks text: on a line that is only text, or with
                   Shift anywhere on the page — a plain click on a setting
                   still changes it. */
                int32_t col = (int32_t)(event.mouse.x / app->settings_cell_w);
                int32_t row = settings_y_to_row(app, event.mouse.y);
                if (app->settings_sel.active) { app->settings_sel.active = false; app->settings_dirty = true; }
                if (keys_layout_click(app, event.mouse.x, event.mouse.y)) { app->settings_dirty = true; break; }   /* RV-217 */
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
                if (app->spath_active && app->spath_view.dragging) {
                    if (editbox_pointer_move(app->settings_renderer, &app->spath_ec, &app->spath_view, event.mouse.x)) app->settings_dirty = true;
                    break;
                }
                if (app->spath_active && app->spath_card.open && app->spath_card.drag != 0) {
                    int32_t sw = 0, sh = 0;
                    rubraview_pal_window_get_size(app->settings_window, &sw, &sh);
                    if (rubraview_card_motion(&app->spath_card, event.mouse.x, event.mouse.y, (double)sw, (double)sh)) app->settings_dirty = true;
                    rubraview_pal_window_set_cursor_shape(app->settings_window, card_cursor_for_part(app->spath_card.drag));
                    break;
                }
                if (app->spath_active && app->spath_card.open) {
                    int32_t part = rubraview_card_hit(&app->spath_card, event.mouse.x, event.mouse.y);
                    rubraview_pal_window_set_cursor_shape(app->settings_window, card_cursor_for_part(part));
                } else {
                    rubraview_pal_window_set_cursor_shape(app->settings_window, RUBRAVIEW_CURSOR_ARROW);
                }
                {   /* RV-252: the circle under the pointer lights */
                    int32_t dot = keys_dot_under(app, event.mouse.x, event.mouse.y);
                    if (dot != g_keys_dot_hover) { g_keys_dot_hover = dot; app->settings_dirty = true; }
                }
                if (scrollbar_motion(app, app->settings_window, event.mouse.x, event.mouse.y)) { app->settings_dirty = true; break; }   /* RV-170 */
                if (scrollbar_hover(app, RVSB_SETTINGS, event.mouse.x, event.mouse.y)) app->settings_dirty = true;   /* its parts light */
                if (app->spath_active || event.mouse.y < app->settings_content_top) app->settings_dirty = true;
                int32_t rr = settings_y_to_row(app, event.mouse.y);
                int32_t tip = event.mouse.y >= app->settings_content_top ? rubraview_settings_view_table_row_at(view, (int32_t)(event.mouse.x / app->settings_cell_w), rr) : -1;
                /* Only the keys' rows say what an action does; the folders' rows are not actions. */
                for (int32_t l = 0; l < FOLDER_LIST_COUNT; ++l) {
                    if (tip > 0 && rubraview_settings_view_table_screen_row(view, cstr(FOLDER_LISTS[l].table), tip) == rr) tip = -1;
                }
                /* The owner (2026-10-17): resting on a setting's name shows its fuller explanation. */
                int32_t help = -1;
                if (tip <= 0 && event.mouse.y >= app->settings_content_top) {
                    int32_t c = (int32_t)(event.mouse.x / app->settings_cell_w);
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
                    int32_t rr = settings_y_to_row(app, event.mouse.y);
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
                app->spath_view.dragging = false;   /* RV-293 */
                if (app->spath_active && app->spath_card.open && rubraview_card_release(&app->spath_card)) {
                    int32_t part = rubraview_card_hit(&app->spath_card, event.mouse.x, event.mouse.y);
                    rubraview_pal_window_set_cursor_shape(app->settings_window, card_cursor_for_part(part));
                    app->settings_dirty = true;
                    break;
                }
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
                if (app->spath_active && (!app->spath_card.open || app->spath_card.focused)) {
                    proven_u8str_view_t typed = { .ptr = (const proven_byte_t *)event.text.utf8, .size = event.text.length };
                    if (event.kind == RUBRAVIEW_WINDOW_EVENT_TEXT) {
                        editbox_text(&app->spath_ec, typed, true);
                        app->spath_composing_length = 0;
                        if (!app->spath_number && app->spath_kind == RUBRAVIEW_TEXT_SEARCH)   /* RV-311: a search follows what is typed */
                            rubraview_settings_set_text(&app->settings, app->spath_section, app->spath_key, app_keep(app, rubraview_textedit_text(&app->spath_ec.te)));
                    } else {
                        size_t n = typed.size < sizeof(app->spath_composing) ? typed.size : sizeof(app->spath_composing) - 1;
                        proven_sys_mem_copy(app->spath_composing, typed.ptr, n);
                        app->spath_composing_length = n;
                    }
                    app->settings_dirty = true;
                }
                break;
            case RUBRAVIEW_WINDOW_EVENT_MOUSE_WHEEL: {
                if (chooser_wheel(app, CHOOSE_IN_SETTINGS, event.mouse.wheel_delta)) break;
                if (event.mouse.x < app->settings_cell_w * view->list_cols) {
                    int32_t sw = 0, sh = 0;
                    rubraview_pal_window_get_size(app->settings_window, &sw, &sh);
                    int32_t pages = (int32_t)(view->doc ? view->doc->page_count : RUBRAVIEW_TAB_COUNT);
                    if (settings_sidebar_visible(app, sh) < pages) {
                        app->settings_page_scroll += event.mouse.wheel_delta > 0 ? -2 : 2;
                        settings_sidebar_clamp(app, sh);
                        app->settings_dirty = true;
                        break;
                    }
                }
                app->settings_sel.active = false;   /* the text under it has moved */
                settings_event(app, rubraview_settings_view_scroll(view, event.mouse.wheel_delta > 0 ? -3 : 3));
                break;
            }
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

static void settings_grid_keep(app_state_t *app, int32_t row, proven_u8str_view_t text) {
    if (row < 0 || row >= (int32_t)(sizeof(app->settings_grid_len) / sizeof(app->settings_grid_len[0]))) return;
    size_t n = text.size < sizeof(app->settings_grid_text[0]) ? text.size : sizeof(app->settings_grid_text[0]) - 1;
    /* Cut on a character, never inside one. */
    while (n > 0 && n < text.size && ((unsigned char)text.ptr[n] & 0xC0u) == 0x80u) --n;
    proven_sys_mem_copy(app->settings_grid_text[row], text.ptr, n);
    app->settings_grid_len[row] = (uint16_t)n;
}

/* A table row with a box to tick in it (an extension's): "[x]" or "[ ]". */
static bool settings_row_has_box(proven_u8str_view_t text) {
    for (size_t i = 0; i + 2 < text.size; ++i)
        if (text.ptr[i] == '[' && (text.ptr[i + 1] == 'x' || text.ptr[i + 1] == ' ') && text.ptr[i + 2] == ']') return true;
    return false;
}

/* 1.25.0: the settings page's scroll bar, at the window's right edge; RV-170: it moves the page. */
bool settings_sb(app_state_t *app, rubraview_scrollbar_t *sb, double bw, double dpi) {
    if (!app->settings_open) return false;
    const rubraview_settings_view_t *v = &app->settings_view;
    int32_t visible = rubraview_settings_view_visible_rows(v);
    int32_t w = 0, h = 0;
    rubraview_pal_window_get_size(app->settings_window, &w, &h);
    double ch = app->settings_cell_h;
    double area_h = (double)h - ch - app->settings_content_top - 2.0 * dpi;
    if (visible <= 0 || area_h <= 2.0 * bw) return false;
    *sb = (rubraview_scrollbar_t){ .area = { (double)w - bw - 2.0 * dpi, app->settings_content_top, bw, area_h },
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
    double dpi = rubraview_pal_window_dpi_scale(app->settings_window);
    settings_layout_buttons(app, w, v->cols);
    settings_sidebar_clamp(app, h);
    rubraview_settings_sources_t sources = { .user = app, .info = settings_info, .table_row = settings_table_row };
    char line[4096];   /* 1.30.1: a 4K window's rows of 3-byte rules fit */

    rubraview_pal_render_begin(r, SETTINGS_BACKGROUND);

    /* The page list (scrolls vertically when window is short). */
    int32_t pages = (int32_t)v->doc->page_count, vis_pages = settings_sidebar_visible(app, h);
    rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ 0, 0, cw * v->list_cols, (double)h }, SETTINGS_PANE, 0.0);
    rubraview_pal_render_draw_text_mono(r, TU8(" Settings"), 0.0, 0.0, fs, SETTINGS_ACCENT);
    if (app->settings_page_scroll > 0)
        rubraview_pal_render_draw_text(r, PROVEN_LIT("\xE2\x96\xB2"), (rubraview_pal_rect_t){ 0.0, ch, cw * v->list_cols, ch }, fs * 0.75, SETTINGS_DIM, RUBRAVIEW_TEXT_CENTER);
    for (int32_t k = 0; k < vis_pages && app->settings_page_scroll + k < pages; ++k) {
        int32_t p = app->settings_page_scroll + k;
        double y = ch * (double)(2 + k);
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
    if (app->settings_page_scroll + vis_pages < pages && (double)h >= ch * 3.0)
        rubraview_pal_render_draw_text(r, PROVEN_LIT("\xE2\x96\xBC"), (rubraview_pal_rect_t){ 0.0, (double)h - ch, cw * v->list_cols, ch }, fs * 0.75, SETTINGS_DIM, RUBRAVIEW_TEXT_CENTER);

    /* The page. Each row's text is kept as drawn, for a marked block to copy. */
    proven_sys_mem_set(app->settings_grid_len, 0, sizeof(app->settings_grid_len));
    double x0 = cw * v->content_col;
    settings_grid_keep(app, 0, rubraview_tr_u8(rubraview_settings_page_title((size_t)v->page)));
    settings_text(app, rubraview_tr_u8(rubraview_settings_page_title((size_t)v->page)), x0, 0.0, SETTINGS_ACCENT);
    double msg_y = (double)h - ch;
    for (size_t i = 0; i < v->line_count; ++i) {
        const rubraview_settings_line_t *ln = &v->lines[i];
        const rubraview_settings_node_t *node = &v->doc->nodes[ln->node];
        for (int32_t k = 0; k < ln->height; ++k) {
            int32_t content_row = ln->row + k - v->scroll;
            if (content_row < 0 || content_row >= rubraview_settings_view_visible_rows(v)) continue;
            double y = settings_row_y(app, RUBRAVIEW_SETTINGS_TOP_ROWS + content_row);
            if (y + ch > msg_y + 0.5) continue;
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
    skip_dirs_draw_frames(app);
    keys_layout_draw(app);   /* RV-217 */

    /* The marked block, shaded over the text it covers. */
    for (int32_t row = 0; row < v->rows; ++row) {
        int32_t c0 = 0, c1 = 0;
        if (!rubraview_grid_selection_row(&app->settings_sel, row, &c0, &c1)) continue;
        int32_t last_col = v->cols - v->content_col - 1;
        if (c1 > last_col) c1 = last_col;
        if (c1 < c0) continue;
        double sy = settings_row_y(app, row);
        if (sy + ch > msg_y + 0.5) continue;
        rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ x0 + cw * c0, sy, cw * (c1 - c0 + 1), ch },
                                       0x704A90D9u, 0.0);
    }
    /* Wrapping top action buttons. */
    for (int32_t b = 0; b < RUBRAVIEW_BUTTON_COUNT; ++b) {
        rubraview_pal_rect_t br = app->settings_btn[b];
        if (b == v->focus_button) {
            rubraview_pal_render_fill_rect(r, br, SETTINGS_FOCUS, 0.0);
        } else if (pal_rect_has(br, app->settings_px, app->settings_py)) {
            rubraview_pal_render_fill_rect(r, br, 0xFF282D34u, 2.0);
        }
        proven_u8str_view_t btxt = rubraview_settings_button_text((rubraview_settings_button_t)b);
        double bx_y = br.y + (br.height - ch) * 0.5;
        if (cw * (double)rubraview_settings_text_cells(btxt) <= br.width + 1.0)
            settings_text_link(app, btxt, br.x, bx_y, SETTINGS_TEXT, '[', ']');
        else {
            char bfit[80];
            rubraview_pal_render_draw_text(r, fit_text_measured(r, bfit, sizeof(bfit), btxt, br.width, fs), br, fs, SETTINGS_LINK, RUBRAVIEW_TEXT_CENTER);
        }
    }

    /* Mask right scrollbar strip so monospace lines never bleed through, then draw footer message bar. */
    {
        rubraview_scrollbar_t sb;
        if (scrollbar_of(app, RVSB_SETTINGS, &sb)) {
            rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ sb.area.x - 4.0 * dpi, app->settings_content_top, (double)w - sb.area.x + 4.0 * dpi, msg_y - app->settings_content_top }, SETTINGS_BACKGROUND, 0.0);
            draw_scrollbar(app, r, RVSB_SETTINGS, app->settings_px, app->settings_py);
        }
    }
    rubraview_pal_render_fill_rect(r, (rubraview_pal_rect_t){ x0 - cw * 0.5, msg_y, (double)w - x0 + cw * 0.5, ch }, SETTINGS_BACKGROUND, 0.0);
    if (app->settings_message[0] && (double)w > x0 + 8.0 * dpi) {
        char mfit[240];
        settings_grid_keep(app, v->rows - 1, cstr(app->settings_message));
        rubraview_pal_render_draw_text(r, fit_text_measured(r, mfit, sizeof(mfit), cstr(app->settings_message), (double)w - x0 - 8.0 * dpi, fs * 0.92),
                                       (rubraview_pal_rect_t){ x0, msg_y, (double)w - x0 - 8.0 * dpi, ch }, fs * 0.92, SETTINGS_DIM, RUBRAVIEW_TEXT_LEFT);
    }

    if (app->settings_tip_shown && app->settings_tip_row > 0 && (size_t)(app->settings_tip_row - 1) < app->keymap.count) {
        /* what the row's action does, under the pointer */
        const rubraview_key_binding_t *b = &app->keymap.bindings[app->settings_tip_row - 1];
        const char *words = rubraview_action_words(b->action);
        if (words) words = rubraview_tr(words);
        char tip[400];
        int n = rubraview_fmt(tip, sizeof(tip), "{}: {}", PROVEN_ARG(rubraview_bytes_view(b->action.ptr, (int)b->action.size)), PROVEN_ARG(words ? words : T("(no words for it)")));
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
    settings_path_draw(app);
    draw_chooser(app, r, CHOOSE_IN_SETTINGS, -1.0, -1.0);   /* 1.24.0 */
    if (!rubraview_pal_render_end(r)) app->settings_dirty = true;
}
