/*
 * D-86: Rubraview in Explorer's right-click menu, and printing.
 *
 * Two menus, written from one table (rubraview_shell_menu_items):
 *
 *  - the classic menu ("Show more options" on Windows 11): registry verbs
 *    under SystemFileAssociations\.ext, which show whoever the type's
 *    default program is. HKCU for this user, HKLM for every user.
 *  - the Windows 11 menu: Windows takes its items only from a package
 *    with identity. rubraview_menu.msix is registered with this program's
 *    folder as its external location, and names rubraview_menu.dll as the
 *    server of one COM class; what that class shows is read from
 *    Software\Rubraview\Menu, written here. Registering an unsigned
 *    package needs an administrator (measured: 0x80073D2B without), so
 *    this is done for every user or not at all.
 */
#ifdef _WIN32
#include "rubraview/pal/pal_window.h"
#include "rubraview/pal/pal_image.h"
#include "rubraview/filemanage.h"
#include "rubraview/shellreq.h"
#include "rubraview/menu_identity.h"
#include "rubraview/version.h"

#include <windows.h>
#include <shlobj.h>
#include <commdlg.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

/* Registering and removing the package through Windows' PackageManager:
   Rubrapack's code, as it is (vendor/rubrapack). */
#include "rp_appx.h"

#define WIDE2(x) L##x
#define WIDE(x) WIDE2(x)

#define MENU_VERB_KEY   L"Rubraview"
#define MENU_ROOT       L"Software\\Rubraview"
#define MENU_CONFIG     L"Software\\Rubraview\\Menu"
#define MENU_CLASS_KEY  MENU_CONFIG L"\\{" WIDE(RUBRAVIEW_MENU_CLSID) L"}"
#define PACKAGE_VALUE   L"MenuPackage"     /* the full name of the package registered, for its removal */
#define PACKAGE_FAMILY  WIDE(RUBRAVIEW_MENU_PACKAGE_NAME) L"_" WIDE(RUBRAVIEW_MENU_PACKAGE_PUBLISHER_ID)

static bool set_string(HKEY root, const WCHAR *subkey, const WCHAR *name, const WCHAR *value) {
    HKEY key = NULL;
    if (RegCreateKeyExW(root, subkey, 0, NULL, 0, KEY_WRITE, NULL, &key, NULL) != ERROR_SUCCESS) return false;
    LONG result = RegSetValueExW(key, name, 0, REG_SZ, (const BYTE*)value, (DWORD)((wcslen(value) + 1) * sizeof(WCHAR)));
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}

static bool get_string(HKEY root, const WCHAR *subkey, const WCHAR *name, WCHAR *out, DWORD out_chars) {
    HKEY key = NULL;
    out[0] = 0;
    if (RegOpenKeyExW(root, subkey, 0, KEY_READ, &key) != ERROR_SUCCESS) return false;
    DWORD type = 0, size = (out_chars - 1) * sizeof(WCHAR);
    bool ok = RegQueryValueExW(key, name, NULL, &type, (BYTE*)out, &size) == ERROR_SUCCESS && type == REG_SZ;
    RegCloseKey(key);
    if (ok) out[size / sizeof(WCHAR)] = 0;
    return ok && out[0];
}

/* Removes a key that holds no value and no key. */
static void delete_if_empty(HKEY root, const WCHAR *subkey) {
    HKEY key = NULL;
    if (RegOpenKeyExW(root, subkey, 0, KEY_READ, &key) != ERROR_SUCCESS) return;
    DWORD keys = 1, values = 1;
    LONG got = RegQueryInfoKeyW(key, NULL, NULL, NULL, &keys, NULL, NULL, &values, NULL, NULL, NULL, NULL);
    RegCloseKey(key);
    if (got == ERROR_SUCCESS && keys == 0 && values == 0) RegDeleteKeyW(root, subkey);
}

static bool can_write_machine(void) {
    HKEY key = NULL;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\Classes", 0, KEY_WRITE, &key) != ERROR_SUCCESS) return false;
    RegCloseKey(key);
    return true;
}

static void widen(const char *text, size_t len, WCHAR *out, int cap) {
    int n = MultiByteToWideChar(CP_UTF8, 0, text, (int)len, out, cap - 1);
    out[n > 0 ? n : 0] = 0;
}

/* This program's path, and the folder it is in. */
static bool program_path(WCHAR *exe, DWORD cap, WCHAR *dir) {
    DWORD n = GetModuleFileNameW(NULL, exe, cap);
    if (n == 0 || n >= cap) return false;
    if (dir) {
        wcscpy(dir, exe);
        WCHAR *slash = wcsrchr(dir, L'\\');
        if (!slash) return false;
        *slash = 0;
    }
    return true;
}

static bool korean_ui(void) {
    return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_KOREAN;
}

/* The next extension of a `;`-separated list, as ".ext"; false after the last. */
static bool next_extension(u8str_t list, size_t *at, WCHAR ext[40], u8str_t *out_narrow) {
    while (*at < list.len) {
        size_t start = *at, end = start;
        while (end < list.len && list.ptr[end] != ';') end++;
        *at = end + 1;
        if (end == start || end - start > 30) continue;
        ext[0] = L'.';
        widen(list.ptr + start, end - start, ext + 1, 38);
        if (out_narrow) *out_narrow = (u8str_t){ .ptr = list.ptr + start, .len = end - start };
        return true;
    }
    return false;
}

static void verb_key_for(const WCHAR *ext, WCHAR *out) {
    wcscpy(out, L"Software\\Classes\\SystemFileAssociations\\");
    wcscat(out, ext);
    wcscat(out, L"\\shell\\" MENU_VERB_KEY);
}

/* ---- the classic menu ---- */

static bool classic_add(HKEY root, const WCHAR *ext, uint32_t kind, const WCHAR *exe) {
    WCHAR key[300], sub[400], text[128], command[MAX_PATH * 2 + 64], icon[MAX_PATH * 2 + 8];
    verb_key_for(ext, key);
    RegDeleteTreeW(root, key);   /* what an earlier choice left here */

    wcscpy(icon, exe);
    wcscat(icon, L",0");
    bool ok = set_string(root, key, L"MUIVerb", L"Rubraview") && set_string(root, key, L"Icon", icon) &&
              /* Empty: the items are the keys under this one's `shell`. */
              set_string(root, key, L"SubCommands", L"");

    size_t count = 0;
    const rubraview_shell_menu_item_t *items = rubraview_shell_menu_items(&count);
    bool korean = korean_ui();
    for (size_t i = 0; ok && i < count; ++i) {
        if (!(items[i].kinds & kind)) continue;
        WCHAR id[32], flag[32];
        widen(items[i].id, strlen(items[i].id), id, 32);
        widen(rubraview_shell_verb_flag(items[i].verb), strlen(rubraview_shell_verb_flag(items[i].verb)), flag, 32);
        const char *label = korean ? items[i].text_ko : items[i].text;
        widen(label, strlen(label), text, 128);

        wcscpy(sub, key);
        wcscat(sub, L"\\shell\\");
        wcscat(sub, id);
        /* Explorer starts the command once for every selected file; the
           viewer gathers them (rubraview_shellreq_collect). "Player" lifts
           the limit of fifteen a plain verb has. */
        ok = set_string(root, sub, L"MUIVerb", text) &&
             set_string(root, sub, L"MultiSelectModel", items[i].single ? L"Single" : L"Player");
        wcscat(sub, L"\\command");
        wcscpy(command, L"\"");
        wcscat(command, exe);
        wcscat(command, L"\" ");
        wcscat(command, flag);
        wcscat(command, L" \"%1\"");
        ok = ok && set_string(root, sub, NULL, command);
    }
    return ok;
}

static void classic_remove(HKEY root, const WCHAR *ext) {
    WCHAR key[300];
    verb_key_for(ext, key);
    RegDeleteTreeW(root, key);
    /* The keys above it go only when nothing else is in them. */
    wcscpy(key, L"Software\\Classes\\SystemFileAssociations\\");
    wcscat(key, ext);
    wcscat(key, L"\\shell");
    delete_if_empty(root, key);
    wcscpy(key, L"Software\\Classes\\SystemFileAssociations\\");
    wcscat(key, ext);
    delete_if_empty(root, key);
}

/* ---- what the Windows 11 item reads ---- */

/* ";jpg;png;" for the extensions of `selection` whose kind is in `kinds`. */
static void on_list(u8str_t selection, uint32_t kinds, WCHAR *out, size_t cap) {
    WCHAR ext[40];
    u8str_t narrow;
    wcscpy(out, L";");
    for (size_t at = 0; next_extension(selection, &at, ext, &narrow);) {
        if (!(rubraview_shell_kind_of(narrow) & kinds)) continue;
        if (wcslen(out) + wcslen(ext) + 2 >= cap) break;
        wcscat(out, ext + 1);
        wcscat(out, L";");
    }
}

static bool config_write(HKEY root, u8str_t selection, const WCHAR *exe) {
    WCHAR on[1024], icon[MAX_PATH * 2 + 8], sub[400], text[128], args[96];
    RegDeleteTreeW(root, MENU_CLASS_KEY);

    wcscpy(icon, exe);
    wcscat(icon, L",0");
    on_list(selection, RUBRAVIEW_SHELL_ALL | RUBRAVIEW_SHELL_ARCHIVES, on, 1024);
    bool ok = set_string(root, MENU_CLASS_KEY, L"Text", L"Rubraview") && set_string(root, MENU_CLASS_KEY, L"Icon", icon) &&
              set_string(root, MENU_CLASS_KEY, L"On", on);

    size_t count = 0;
    const rubraview_shell_menu_item_t *items = rubraview_shell_menu_items(&count);
    WCHAR names[512];
    size_t names_len = 0;
    for (size_t i = 0; ok && i < count; ++i) {
        WCHAR id[32], flag[32];
        widen(items[i].id, strlen(items[i].id), id, 32);
        widen(rubraview_shell_verb_flag(items[i].verb), strlen(rubraview_shell_verb_flag(items[i].verb)), flag, 32);
        if (names_len + wcslen(id) + 2 >= 512) break;
        wcscpy(names + names_len, id);
        names_len += wcslen(id) + 1;

        wcscpy(sub, MENU_CLASS_KEY L"\\");
        wcscat(sub, id);
        widen(items[i].text, strlen(items[i].text), text, 128);
        ok = set_string(root, sub, L"Text", text);
        widen(items[i].text_ko, strlen(items[i].text_ko), text, 128);
        ok = ok && set_string(root, sub, L"Text.ko", text);
        /* Every selected file on one command line, and the word that
           says so: nothing more is coming, the viewer need not wait. */
        wcscpy(args, flag);
        wcscat(args, L" --whole %*");
        on_list(selection, items[i].kinds, on, 1024);
        ok = ok && set_string(root, sub, L"Command", exe) && set_string(root, sub, L"Args", args) &&
             set_string(root, sub, L"Multi", items[i].single ? L"single" : L"one") && set_string(root, sub, L"On", on);
    }
    if (ok) {
        names[names_len++] = 0;   /* the list's own end */
        HKEY key = NULL;
        ok = RegCreateKeyExW(root, MENU_CLASS_KEY, 0, NULL, 0, KEY_WRITE, NULL, &key, NULL) == ERROR_SUCCESS;
        if (ok) {
            ok = RegSetValueExW(key, L"Items", 0, REG_MULTI_SZ, (const BYTE*)names, (DWORD)(names_len * sizeof(WCHAR))) == ERROR_SUCCESS;
            RegCloseKey(key);
        }
    }
    return ok;
}

static void config_remove(HKEY root) {
    RegDeleteTreeW(root, MENU_CLASS_KEY);
    delete_if_empty(root, MENU_CONFIG);
    delete_if_empty(root, MENU_ROOT);
}

/* ---- the package ---- */

static bool file_exists(const WCHAR *path) {
    DWORD a = GetFileAttributesW(path);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

bool rubraview_pal_shell_menu_parts_present(void) {
    WCHAR exe[MAX_PATH * 2], dir[MAX_PATH * 2], path[MAX_PATH * 2 + 40];
    if (!program_path(exe, MAX_PATH * 2, dir)) return false;
    wcscpy(path, dir);
    wcscat(path, L"\\" WIDE(RUBRAVIEW_MENU_DLL_FILE));
    if (!file_exists(path)) return false;
    wcscpy(path, dir);
    wcscat(path, L"\\" WIDE(RUBRAVIEW_MENU_PACKAGE_FILE));
    return file_exists(path);
}

/* The package registered before, by the name kept when it was. */
static void package_remove(void) {
    WCHAR full[256];
    if (!get_string(HKEY_LOCAL_MACHINE, MENU_ROOT, PACKAGE_VALUE, full, 256)) return;
    appx_job_t job = { .full_name = full, .family = PACKAGE_FAMILY, .machine = true, .remove = true };
    (void)appx_run(&job);
    HKEY key = NULL;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, MENU_ROOT, 0, KEY_SET_VALUE, &key) == ERROR_SUCCESS) {
        RegDeleteValueW(key, PACKAGE_VALUE);
        RegCloseKey(key);
    }
}

static bool package_add(void) {
    WCHAR exe[MAX_PATH * 2], dir[MAX_PATH * 2], package[MAX_PATH * 2 + 40], full[256];
    if (!program_path(exe, MAX_PATH * 2, dir)) return false;
    wcscpy(package, dir);
    wcscat(package, L"\\" WIDE(RUBRAVIEW_MENU_PACKAGE_FILE));

    /* An earlier one may name another folder, or be an older version. */
    package_remove();

    appx_job_t job = { .package = package, .external = dir, .family = PACKAGE_FAMILY, .machine = true, .allow_unsigned = true };
    if (FAILED(appx_run(&job))) return false;

    wsprintfW(full, L"%ls_%d.%d.%d.0_x64__%ls", WIDE(RUBRAVIEW_MENU_PACKAGE_NAME), RUBRAVIEW_VERSION_MAJOR,
              RUBRAVIEW_VERSION_MINOR, RUBRAVIEW_VERSION_PATCH, WIDE(RUBRAVIEW_MENU_PACKAGE_PUBLISHER_ID));
    return set_string(HKEY_LOCAL_MACHINE, MENU_ROOT, PACKAGE_VALUE, full);
}

/* ---- both, from the viewer ---- */

rubraview_shell_menu_result_t rubraview_pal_shell_menu_register(u8str_t selection, bool all_users) {
    WCHAR exe[MAX_PATH * 2], ext[40];
    u8str_t narrow;
    if (all_users && !can_write_machine()) return RUBRAVIEW_SHELL_MENU_FAILED;
    if (!program_path(exe, MAX_PATH * 2, NULL)) return RUBRAVIEW_SHELL_MENU_FAILED;
    HKEY root = all_users ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;

    /* The registry is made to match the choice: ours is taken off every
       extension we know, then put on the chosen ones. */
    u8str_t every = rubraview_shell_extensions_all();
    for (size_t at = 0; next_extension(every, &at, ext, NULL);) classic_remove(root, ext);
    bool ok = true;
    for (size_t at = 0; next_extension(selection, &at, ext, &narrow);) {
        uint32_t kind = rubraview_shell_kind_of(narrow);
        if (kind && !classic_add(root, ext, kind, exe)) ok = false;
    }

    rubraview_shell_menu_result_t result = ok ? RUBRAVIEW_SHELL_MENU_CLASSIC : RUBRAVIEW_SHELL_MENU_FAILED;
    if (ok && all_users) {
        if (!rubraview_pal_shell_menu_parts_present()) {
            result = RUBRAVIEW_SHELL_MENU_CLASSIC_NO_PARTS;
        } else if (config_write(root, selection, exe) && package_add()) {
            result = RUBRAVIEW_SHELL_MENU_BOTH;
        } else {
            result = RUBRAVIEW_SHELL_MENU_CLASSIC_REFUSED;
        }
    }
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
    return result;
}

bool rubraview_pal_shell_menu_unregister(bool all_users) {
    WCHAR ext[40];
    if (all_users && !can_write_machine()) return false;
    HKEY root = all_users ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
    u8str_t every = rubraview_shell_extensions_all();
    for (size_t at = 0; next_extension(every, &at, ext, NULL);) classic_remove(root, ext);
    if (all_users) package_remove();
    config_remove(root);
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
    return true;
}

/* ---- printing ---- */

/* `turns` quarter turns clockwise of 32-bit pixels into `dst`, which is
   h x w for an odd count. */
static void turn_pixels(const uint32_t *src, int32_t w, int32_t h, int32_t turns, uint32_t *dst) {
    size_t W = (size_t)w, H = (size_t)h;
    for (size_t y = 0; y < H; ++y) {
        const uint32_t *row = src + y * W;
        for (size_t x = 0; x < W; ++x) {
            size_t at = turns == 1 ? x * H + (H - 1 - y)
                      : turns == 2 ? (H - 1 - y) * W + (W - 1 - x)
                      :              (W - 1 - x) * H + y;
            dst[at] = row[x];
        }
    }
}

/* One picture in its cell of the sheet (D-87). A file that cannot be
   read takes no cell: `*on_sheet` counts the pictures on the open sheet,
   and a sheet is begun for the first of them. */
static bool print_one(HDC dc, u8str_t path, const rubraview_print_options_t *options, int32_t *on_sheet) {
    int32_t w = 0, h = 0;
    if (!rubraview_pal_image_size(path, &w, &h) || w <= 0 || h <= 0) return false;

    /* The picture gets memory of its own: decoded, and once more for the
       decoder's conversion and a turn. */
    size_t pixels = (size_t)w * (size_t)h;
    if (pixels > ((size_t)1 << 28)) return false;
    size_t room = pixels * 4u * 3u + (16u << 20);
    void *memory = malloc(room);
    if (!memory) return false;
    proven_arena_t arena = proven_arena_create((proven_mem_mut_t){ .ptr = (proven_byte_t*)memory, .size = room });
    rubraview_pixbuf_t picture = rubraview_pal_image_read_pixels(&arena, path, NULL, 0, true);
    if (!picture.pixels || picture.width <= 0 || picture.height <= 0 || picture.stride != picture.width * 4 ||
        picture.format != RUBRAVIEW_PIXFMT_RGBA8) {
        free(memory);
        return false;
    }
    w = picture.width;
    h = picture.height;
    pixels = (size_t)w * (size_t)h;
    /* Paper is white: see-through pixels are laid on it. The reader hands
       over red first with the see-through apart; a Windows bitmap wants
       blue first. */
    uint8_t *p = picture.pixels;
    for (size_t i = 0; i < pixels; ++i, p += 4) {
        unsigned a = p[3], rest = 255u - a;
        unsigned r = (p[0] * a + 255u * rest + 127u) / 255u;
        unsigned g = (p[1] * a + 255u * rest + 127u) / 255u;
        unsigned bl = (p[2] * a + 255u * rest + 127u) / 255u;
        p[0] = (uint8_t)bl;
        p[1] = (uint8_t)g;
        p[2] = (uint8_t)r;
        p[3] = 255;
    }

    int32_t sheet_w = GetDeviceCaps(dc, HORZRES), sheet_h = GetDeviceCaps(dc, VERTRES);
    int32_t dpi_x = GetDeviceCaps(dc, LOGPIXELSX), dpi_y = GetDeviceCaps(dc, LOGPIXELSY);
    int32_t cx = 0, cy = 0, cw = 0, ch = 0;
    rubraview_print_cell(sheet_w, sheet_h, dpi_x, dpi_y, options, *on_sheet, &cx, &cy, &cw, &ch);
    rubraview_print_placement_t place;
    rubraview_print_options_t chosen = *options;
    if (!rubraview_print_place(cx, cy, cw, ch, dpi_x, dpi_y, w, h, &chosen, &place)) {
        free(memory);
        return false;
    }

    const uint8_t *bits = picture.pixels;
    if (place.quarter_turns != 0) {
        proven_result_mem_mut_t res = proven_arena_alloc(&arena, pixels * 4u);
        if (proven_is_ok(res.err)) {
            turn_pixels((const uint32_t*)(const void*)picture.pixels, w, h, place.quarter_turns, (uint32_t*)(void*)res.value.ptr);
            bits = res.value.ptr;
        } else {
            /* No room to turn it: printed as it stands. */
            chosen.turn = RUBRAVIEW_PRINT_TURN_NONE;
            rubraview_print_place(cx, cy, cw, ch, dpi_x, dpi_y, w, h, &chosen, &place);
        }
    }

    bool ok = false;
    if (*on_sheet > 0 || StartPage(dc) > 0) {
        /* The rows from the first one printed on: a bitmap's own row
           numbering is then not asked about, only the column. */
        BITMAPINFO info = { 0 };
        info.bmiHeader.biSize = sizeof(info.bmiHeader);
        info.bmiHeader.biWidth = place.turned_w;
        info.bmiHeader.biHeight = -place.src_h;   /* top row first */
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        const uint8_t *from = bits + (size_t)place.src_y * (size_t)place.turned_w * 4u;
        SetStretchBltMode(dc, HALFTONE);
        SetBrushOrgEx(dc, 0, 0, NULL);
        ok = StretchDIBits(dc, place.dst_x, place.dst_y, place.dst_w, place.dst_h,
                           place.src_x, 0, place.src_w, place.src_h, from, &info, DIB_RGB_COLORS, SRCCOPY) > 0;
        (*on_sheet)++;
    }
    free(memory);
    return ok;
}

bool rubraview_pal_print_pictures(void *parent_window_handle, const u8str_t *paths, size_t count,
                                  const rubraview_print_options_t *options, size_t *out_printed) {
    if (out_printed) *out_printed = 0;
    if (!paths || count == 0) return false;
    rubraview_print_options_t chosen = options ? *options : rubraview_print_options_default();
    rubraview_print_options_clamp(&chosen);

    PRINTDLGEXW dialog = { 0 };
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = parent_window_handle ? (HWND)parent_window_handle : GetDesktopWindow();
    dialog.Flags = PD_RETURNDC | PD_NOPAGENUMS | PD_NOSELECTION | PD_NOCURRENTPAGE | PD_USEDEVMODECOPIESANDCOLLATE;
    dialog.nStartPage = START_PAGE_GENERAL;
    dialog.nCopies = 1;
    if (PrintDlgExW(&dialog) != S_OK || dialog.dwResultAction != PD_RESULT_PRINT || !dialog.hDC) {
        if (dialog.hDC) DeleteDC(dialog.hDC);
        if (dialog.hDevMode) GlobalFree(dialog.hDevMode);
        if (dialog.hDevNames) GlobalFree(dialog.hDevNames);
        return false;   /* cancelled, or there is no printer */
    }

    HDC dc = dialog.hDC;
    DOCINFOW doc = { .cbSize = sizeof(doc), .lpszDocName = L"Rubraview" };
    size_t printed = 0;
    int32_t on_sheet = 0;
    bool started = StartDocW(dc, &doc) > 0;
    for (size_t i = 0; started && i < count; ++i) {
        if (print_one(dc, paths[i], &chosen, &on_sheet)) printed++;
        if (on_sheet >= chosen.per_sheet) {
            EndPage(dc);
            on_sheet = 0;
        }
    }
    if (started) {
        if (on_sheet > 0) EndPage(dc);
        /* A job with nothing on it is not sent. */
        if (printed > 0) EndDoc(dc);
        else AbortDoc(dc);
    }
    DeleteDC(dc);
    if (dialog.hDevMode) GlobalFree(dialog.hDevMode);
    if (dialog.hDevNames) GlobalFree(dialog.hDevNames);
    if (out_printed) *out_printed = printed;
    return started;
}

#endif /* _WIN32 */
