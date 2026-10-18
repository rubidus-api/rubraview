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
#include <winspool.h>
#include <stdio.h>
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
/* RV-124: while this value is there the Store's copy hides its Windows 11 items
   (rubrapack `hide-when`), so one computer shows one Rubraview there. A key of
   its own: the installer deletes it whole when it removes the program. */
#define MARKER_KEY      L"Software\\Rubraview\\Win11Menu"
#define MARKER_VALUE    L"Registered"
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
    RegDeleteKeyW(HKEY_LOCAL_MACHINE, MARKER_KEY);   /* first: no package recorded leaves no marker either */
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
    (void)set_string(HKEY_LOCAL_MACHINE, MARKER_KEY, MARKER_VALUE, L"1");
    return set_string(HKEY_LOCAL_MACHINE, MENU_ROOT, PACKAGE_VALUE, full);
}

/* ---- both, from the viewer ---- */

/* Windows 11 shows a package's items under "Show more options" as well as
   in its own menu (seen on build 26200), so the registry's verb beside a
   registered package is Rubraview twice there. Windows 10 is asked nothing
   of the kind: its one menu keeps the registry's verb. */
static bool package_shows_in_classic_menu(void) {
    typedef LONG (WINAPI *rtl_get_version_t)(OSVERSIONINFOW*);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    rtl_get_version_t get = ntdll ? (rtl_get_version_t)(void*)GetProcAddress(ntdll, "RtlGetVersion") : NULL;
    OSVERSIONINFOW v = { .dwOSVersionInfoSize = sizeof(v) };
    return get && get(&v) == 0 && v.dwBuildNumber >= 22000;
}

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
        if (rubraview_pal_shell_other_copies().store) {
            /* D-154: the Store's copy brings its own Windows 11 menu; a second
               "Rubraview" there would be this one's. The classic menu stays
               ("Show more options"), and the item that says where this copy is. */
            (void)config_write(root, selection, exe);
            package_remove();
            result = RUBRAVIEW_SHELL_MENU_CLASSIC_STORE;
        } else if (!rubraview_pal_shell_menu_parts_present()) {
            result = RUBRAVIEW_SHELL_MENU_CLASSIC_NO_PARTS;
        } else if (config_write(root, selection, exe) && package_add()) {
            result = RUBRAVIEW_SHELL_MENU_BOTH;
            /* D-116: the package carries both menus there; ours comes off
               the registry for every user, and for this one. */
            if (package_shows_in_classic_menu()) {
                for (size_t at = 0; next_extension(every, &at, ext, NULL);) {
                    classic_remove(HKEY_LOCAL_MACHINE, ext);
                    classic_remove(HKEY_CURRENT_USER, ext);
                }
            }
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

/* ---- D-110: every trace, an older version's too ---- */

#define WIPE_NAMES 256

/* The names of the keys directly under `subkey`, as many as fit. */
static DWORD child_names(HKEY root, const WCHAR *subkey, WCHAR names[][128], DWORD cap) {
    HKEY key = NULL;
    DWORD count = 0;
    if (RegOpenKeyExW(root, subkey, 0, KEY_READ, &key) != ERROR_SUCCESS) return 0;
    for (DWORD i = 0; count < cap; ++i) {
        DWORD len = 128;
        LONG got = RegEnumKeyExW(key, i, names[count], &len, NULL, NULL, NULL, NULL);
        if (got == ERROR_NO_MORE_ITEMS) break;
        if (got == ERROR_SUCCESS) count++;   /* a longer name is not one of ours */
    }
    RegCloseKey(key);
    return count;
}

static bool wide_starts(const WCHAR *text, const WCHAR *lower_word) {
    for (; *lower_word; ++text, ++lower_word) {
        WCHAR c = *text >= L'A' && *text <= L'Z' ? (WCHAR)(*text - L'A' + L'a') : *text;
        if (c != *lower_word) return false;
    }
    return true;
}

/* Is the verb `shell_key`\`verb` Rubraview's? By its name, or by what its
   command starts. */
static bool verb_is_ours(HKEY root, const WCHAR *shell_key, const WCHAR *verb) {
    WCHAR sub[520], command[1024];
    char name8[400], command8[3100];
    if (wcslen(shell_key) + wcslen(verb) + 12 >= 520) return false;
    wcscpy(sub, shell_key);
    wcscat(sub, L"\\");
    wcscat(sub, verb);
    wcscat(sub, L"\\command");
    command[0] = 0;
    HKEY key = NULL;
    if (RegOpenKeyExW(root, sub, 0, KEY_READ, &key) == ERROR_SUCCESS) {
        DWORD type = 0, size = sizeof(command) - sizeof(WCHAR);
        if (RegQueryValueExW(key, NULL, NULL, &type, (BYTE*)command, &size) == ERROR_SUCCESS &&
            (type == REG_SZ || type == REG_EXPAND_SZ)) {
            command[size / sizeof(WCHAR)] = 0;
        } else {
            command[0] = 0;
        }
        RegCloseKey(key);
    }
    int n = WideCharToMultiByte(CP_UTF8, 0, verb, -1, name8, sizeof(name8), NULL, NULL);
    int c = WideCharToMultiByte(CP_UTF8, 0, command, -1, command8, sizeof(command8), NULL, NULL);
    if (n <= 0) return false;
    return rubraview_shell_entry_is_ours((u8str_t){ .ptr = name8, .len = (size_t)n - 1 },
                                         (u8str_t){ .ptr = command8, .len = c > 0 ? (size_t)c - 1 : 0 });
}

/* An extension still pointing at a type of ours — one an older version
   listed and this one does not, so Remove never came by: it goes back to
   the program it had, as Remove gives one back, and we leave its "Open
   with" list. 1 when something was ours. */
static int32_t wipe_extension(HKEY root, const WCHAR *ext) {
    WCHAR sub[300], value[128], was[128];
    if (wcslen(ext) > 60) return 0;
    wcscpy(sub, L"Software\\Classes\\");
    wcscat(sub, ext);
    HKEY key = NULL;
    if (RegOpenKeyExW(root, sub, 0, KEY_READ | KEY_SET_VALUE, &key) != ERROR_SUCCESS) return 0;
    int32_t found = 0;
    DWORD type = 0, size = sizeof(value) - sizeof(WCHAR);
    value[0] = 0;
    bool ours = RegQueryValueExW(key, NULL, NULL, &type, (BYTE*)value, &size) == ERROR_SUCCESS && type == REG_SZ;
    if (ours) value[size / sizeof(WCHAR)] = 0;
    ours = ours && wide_starts(value, L"rubraview.");
    size = sizeof(was) - sizeof(WCHAR);
    was[0] = 0;
    bool had = RegQueryValueExW(key, L"Rubraview.Was", NULL, &type, (BYTE*)was, &size) == ERROR_SUCCESS && type == REG_SZ;
    if (had) was[size / sizeof(WCHAR)] = 0;
    if (ours) {
        if (had && was[0]) RegSetValueExW(key, NULL, 0, REG_SZ, (const BYTE*)was, (DWORD)((wcslen(was) + 1) * sizeof(WCHAR)));
        else RegDeleteValueW(key, NULL);
        found = 1;
    }
    if (had) { RegDeleteValueW(key, L"Rubraview.Was"); found = 1; }
    RegCloseKey(key);

    WCHAR with[340];
    wcscpy(with, sub);
    wcscat(with, L"\\OpenWithProgids");
    HKEY open_with = NULL;
    if (RegOpenKeyExW(root, with, 0, KEY_READ | KEY_SET_VALUE, &open_with) == ERROR_SUCCESS) {
        for (DWORD i = 0;;) {
            DWORD len = 128;
            LONG got = RegEnumValueW(open_with, i, value, &len, NULL, NULL, NULL, NULL);
            if (got == ERROR_NO_MORE_ITEMS) break;
            if (got == ERROR_SUCCESS && wide_starts(value, L"rubraview.") && RegDeleteValueW(open_with, value) == ERROR_SUCCESS) {
                found = 1;   /* the next value has this index now */
            } else {
                i++;
            }
        }
        RegCloseKey(open_with);
        if (found) delete_if_empty(root, with);
    }
    /* The extension's own key stays even when empty: the caller is walking the keys beside it. */
    return found;
}

/* Our verbs under `owner`\shell go; the key itself too when `tidy` and
   nothing is left in it. */
static int32_t wipe_verbs(HKEY root, const WCHAR *owner, bool tidy) {
    static WCHAR verbs[64][128];
    WCHAR shell[400], sub[540];
    int32_t removed = 0;
    if (wcslen(owner) + 8 >= 400) return 0;
    wcscpy(shell, owner);
    wcscat(shell, L"\\shell");
    DWORD count = child_names(root, shell, verbs, 64);
    for (DWORD i = 0; i < count; ++i) {
        if (!verb_is_ours(root, shell, verbs[i])) continue;
        wcscpy(sub, shell);
        wcscat(sub, L"\\");
        wcscat(sub, verbs[i]);
        if (RegDeleteTreeW(root, sub) == ERROR_SUCCESS) removed++;
    }
    if (count > 0) delete_if_empty(root, shell);
    if (tidy) delete_if_empty(root, owner);
    return removed;
}

typedef LONG (WINAPI *packages_by_family_fn)(PCWSTR, UINT32*, PWSTR*, UINT32*, WCHAR*);

/* Every package of the menu's family this user has, whatever its version. */
static int32_t wipe_packages(void) {
    HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    packages_by_family_fn list = kernel ? (packages_by_family_fn)(void*)GetProcAddress(kernel, "GetPackagesByPackageFamily") : NULL;
    if (!list) return 0;
    UINT32 count = 0, chars = 0;
    if (list(PACKAGE_FAMILY, &count, NULL, &chars, NULL) != ERROR_INSUFFICIENT_BUFFER || count == 0 || count > 32) return 0;
    PWSTR *names = (PWSTR*)calloc(count, sizeof(PWSTR));
    WCHAR *text = (WCHAR*)calloc(chars + 1, sizeof(WCHAR));
    int32_t removed = 0;
    if (names && text && list(PACKAGE_FAMILY, &count, names, &chars, text) == ERROR_SUCCESS) {
        for (UINT32 i = 0; i < count; ++i) {
            appx_job_t job = { .full_name = names[i], .family = PACKAGE_FAMILY, .machine = true, .remove = true };
            if (SUCCEEDED(appx_run(&job))) removed++;
        }
    }
    free(names);
    free(text);
    return removed;
}

int32_t rubraview_pal_shell_menu_wipe(bool all_users) {
    static WCHAR names[WIPE_NAMES][128];
    static const WCHAR *const EVERYWHERE[] = {
        L"Software\\Classes\\*", L"Software\\Classes\\Directory", L"Software\\Classes\\Directory\\Background",
        L"Software\\Classes\\Folder", L"Software\\Classes\\Drive", L"Software\\Classes\\AllFilesystemObjects",
    };
    WCHAR sub[400], ext[40];
    if (all_users && !can_write_machine()) return -1;
    HKEY root = all_users ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
    int32_t removed = 0;

    /* The menu on a file type, whoever opens it: any type, not only the ones this version knows. */
    DWORD count = child_names(root, L"Software\\Classes\\SystemFileAssociations", names, WIPE_NAMES);
    for (DWORD i = 0; i < count; ++i) {
        wcscpy(sub, L"Software\\Classes\\SystemFileAssociations\\");
        wcscat(sub, names[i]);
        removed += wipe_verbs(root, sub, true);
    }
    for (size_t i = 0; i < sizeof(EVERYWHERE) / sizeof(EVERYWHERE[0]); ++i) removed += wipe_verbs(root, EVERYWHERE[i], false);
    u8str_t every = rubraview_shell_extensions_all();
    for (size_t at = 0; next_extension(every, &at, ext, NULL);) {
        wcscpy(sub, L"Software\\Classes\\");
        wcscat(sub, ext);
        removed += wipe_verbs(root, sub, false);
    }

    /* A type of ours left by an older list of extensions, and the programs "Open with" lists by
       their file's name (a download keeps its version in it). The types of this version's list
       went with the file types, which gave each extension back first. */
    HKEY classes = NULL;
    if (RegOpenKeyExW(root, L"Software\\Classes", 0, KEY_READ, &classes) == ERROR_SUCCESS) {
        count = 0;
        for (DWORD i = 0; count < WIPE_NAMES; ++i) {
            DWORD len = 128;
            LONG got = RegEnumKeyExW(classes, i, names[count], &len, NULL, NULL, NULL, NULL);
            if (got == ERROR_NO_MORE_ITEMS) break;
            if (got != ERROR_SUCCESS) continue;
            /* An extension this version no longer lists may still point at such a type. */
            if (names[count][0] == L'.') removed += wipe_extension(root, names[count]);
            else if (wide_starts(names[count], L"rubraview.")) count++;
        }
        RegCloseKey(classes);
        for (DWORD i = 0; i < count; ++i) {
            wcscpy(sub, L"Software\\Classes\\");
            wcscat(sub, names[i]);
            if (RegDeleteTreeW(root, sub) == ERROR_SUCCESS) removed++;
        }
    }
    count = child_names(root, L"Software\\Classes\\Applications", names, WIPE_NAMES);
    for (DWORD i = 0; i < count; ++i) {
        size_t len = wcslen(names[i]);
        if (len < 13 || !wide_starts(names[i], L"rubraview") || !wide_starts(names[i] + len - 4, L".exe")) continue;
        wcscpy(sub, L"Software\\Classes\\Applications\\");
        wcscat(sub, names[i]);
        if (RegDeleteTreeW(root, sub) == ERROR_SUCCESS) removed++;
    }

    /* What the Windows 11 item reads, and the packages themselves. */
    HKEY config = NULL;
    if (RegOpenKeyExW(root, MENU_CONFIG, 0, KEY_READ, &config) == ERROR_SUCCESS) {
        RegCloseKey(config);
        if (RegDeleteTreeW(root, MENU_CONFIG) == ERROR_SUCCESS) removed++;
    }
    if (all_users) {
        removed += wipe_packages();
        HKEY key = NULL;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, MENU_ROOT, 0, KEY_SET_VALUE, &key) == ERROR_SUCCESS) {
            RegDeleteValueW(key, PACKAGE_VALUE);
            RegCloseKey(key);
        }
    }
    delete_if_empty(root, MENU_ROOT);
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
    return removed;
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

/* The pictures onto the DC's sheets, `per_sheet` a sheet: one document. */
static bool print_on_dc(HDC dc, const u8str_t *paths, size_t count, const rubraview_print_options_t *chosen,
                        const WCHAR *output, size_t *out_printed) {
    DOCINFOW doc = { .cbSize = sizeof(doc), .lpszDocName = L"Rubraview", .lpszOutput = output };
    size_t printed = 0;
    int32_t on_sheet = 0;
    bool started = StartDocW(dc, &doc) > 0;
    for (size_t i = 0; started && i < count; ++i) {
        if (print_one(dc, paths[i], chosen, &on_sheet)) printed++;
        if (on_sheet >= chosen->per_sheet) {
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
    *out_printed = printed;
    return started;
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
    size_t printed = 0;
    bool started = print_on_dc(dc, paths, count, &chosen, NULL, &printed);
    DeleteDC(dc);
    if (dialog.hDevMode) GlobalFree(dialog.hDevMode);
    if (dialog.hDevNames) GlobalFree(dialog.hDevNames);
    if (out_printed) *out_printed = printed;
    return started;
}

/* ---- 1.19.0 (owner, 2026-10-18: "인쇄할 때 용지 선택 ... 프리뷰 ... 프린터 선택도"): the printer,
   its paper and the way the sheet lies chosen in Rubraview's own panel, and printed without Windows' dialog ---- */

static bool wide_of(u8str_t s, WCHAR *out, int cap) {
    if (s.len == 0 || s.len >= (size_t)cap) return false;
    int n = MultiByteToWideChar(CP_UTF8, 0, s.ptr, (int)s.len, out, cap - 1);
    if (n <= 0) return false;
    out[n] = 0;
    return true;
}

static u8str_t utf8_of(proven_arena_t *arena, const WCHAR *w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    if (n <= 1) return (u8str_t){ .ptr = "", .len = 0 };
    proven_result_mem_mut_t res = proven_arena_alloc(arena, (size_t)n);
    if (!proven_is_ok(res.err)) return (u8str_t){ .ptr = "", .len = 0 };
    WideCharToMultiByte(CP_UTF8, 0, w, -1, (char*)res.value.ptr, n, NULL, NULL);
    return (u8str_t){ .ptr = (const char*)res.value.ptr, .len = (size_t)n - 1 };
}

size_t rubraview_pal_printers(proven_arena_t *arena, u8str_t *out, size_t capacity, int32_t *out_default) {
    if (out_default) *out_default = -1;
    if (!arena || !out || capacity == 0) return 0;
    DWORD need = 0, got = 0;
    DWORD flags = PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS;
    EnumPrintersW(flags, NULL, 4, NULL, 0, &need, &got);
    if (need == 0) return 0;
    BYTE *buf = (BYTE*)malloc(need);
    if (!buf) return 0;
    size_t count = 0;
    if (EnumPrintersW(flags, NULL, 4, buf, need, &need, &got)) {
        WCHAR def[512];
        DWORD def_len = 512;
        if (!GetDefaultPrinterW(def, &def_len)) def[0] = 0;
        const PRINTER_INFO_4W *info = (const PRINTER_INFO_4W*)(const void*)buf;
        for (DWORD i = 0; i < got && count < capacity; ++i) {
            if (!info[i].pPrinterName) continue;
            u8str_t name = utf8_of(arena, info[i].pPrinterName);
            if (name.len == 0) continue;
            if (out_default && def[0] && lstrcmpiW(def, info[i].pPrinterName) == 0) *out_default = (int32_t)count;
            out[count++] = name;
        }
    }
    free(buf);
    return count;
}

size_t rubraview_pal_printer_papers(u8str_t printer, rubraview_printer_paper_t *out, size_t capacity, int32_t *out_default_id) {
    if (out_default_id) *out_default_id = 0;
    WCHAR name[512];
    if (!out || capacity == 0 || !wide_of(printer, name, 512)) return 0;
    int n = DeviceCapabilitiesW(name, NULL, DC_PAPERS, NULL, NULL);
    if (n <= 0 || n > 4096) return 0;
    WORD *ids = (WORD*)calloc((size_t)n, sizeof(WORD));
    WCHAR *names = (WCHAR*)calloc((size_t)n, 64 * sizeof(WCHAR));
    POINT *sizes = (POINT*)calloc((size_t)n, sizeof(POINT));
    size_t count = 0;
    if (ids && names && sizes &&
        DeviceCapabilitiesW(name, NULL, DC_PAPERS, (LPWSTR)ids, NULL) == n &&
        DeviceCapabilitiesW(name, NULL, DC_PAPERNAMES, names, NULL) == n &&
        DeviceCapabilitiesW(name, NULL, DC_PAPERSIZE, (LPWSTR)(void*)sizes, NULL) == n) {
        for (int i = 0; i < n && count < capacity; ++i) {
            if (sizes[i].x <= 0 || sizes[i].y <= 0) continue;
            rubraview_printer_paper_t *p = &out[count++];
            *p = (rubraview_printer_paper_t){ .id = ids[i], .w_tenth_mm = sizes[i].x, .h_tenth_mm = sizes[i].y };
            WCHAR one[65];
            memcpy(one, names + (size_t)i * 64u, 64 * sizeof(WCHAR));
            one[64] = 0;
            int m = WideCharToMultiByte(CP_UTF8, 0, one, -1, p->name, (int)sizeof(p->name), NULL, NULL);
            if (m <= 1) snprintf(p->name, sizeof(p->name), "%d x %d mm", (int)(sizes[i].x / 10), (int)(sizes[i].y / 10));
        }
    }
    free(ids); free(names); free(sizes);
    /* the printer's own paper, as its driver has it now */
    HANDLE h = NULL;
    if (out_default_id && OpenPrinterW(name, &h, NULL) && h) {
        LONG size = DocumentPropertiesW(NULL, h, name, NULL, NULL, 0);
        DEVMODEW *dm = size > 0 ? (DEVMODEW*)malloc((size_t)size) : NULL;
        if (dm && DocumentPropertiesW(NULL, h, name, dm, NULL, DM_OUT_BUFFER) == IDOK && (dm->dmFields & DM_PAPERSIZE)) *out_default_id = dm->dmPaperSize;
        free(dm);
        ClosePrinter(h);
    }
    return count;
}

/* A DC for the printer with this paper, this way up and copies; NULL when it cannot. */
static HDC printer_dc(u8str_t printer, int32_t paper_id, int32_t orientation, int32_t copies) {
    WCHAR name[512];
    if (!wide_of(printer, name, 512)) return NULL;
    HANDLE h = NULL;
    if (!OpenPrinterW(name, &h, NULL) || !h) return NULL;
    HDC dc = NULL;
    LONG size = DocumentPropertiesW(NULL, h, name, NULL, NULL, 0);
    DEVMODEW *dm = size > 0 ? (DEVMODEW*)malloc((size_t)size) : NULL;
    if (dm && DocumentPropertiesW(NULL, h, name, dm, NULL, DM_OUT_BUFFER) == IDOK) {
        if (paper_id > 0) { dm->dmPaperSize = (short)paper_id; dm->dmFields |= DM_PAPERSIZE; dm->dmFields &= ~(DWORD)(DM_PAPERLENGTH | DM_PAPERWIDTH); }
        if (orientation == 1 || orientation == 2) { dm->dmOrientation = orientation == 2 ? DMORIENT_LANDSCAPE : DMORIENT_PORTRAIT; dm->dmFields |= DM_ORIENTATION; }
        if (copies > 1) { dm->dmCopies = (short)copies; dm->dmFields |= DM_COPIES; }
        if (DocumentPropertiesW(NULL, h, name, dm, dm, DM_IN_BUFFER | DM_OUT_BUFFER) == IDOK) dc = CreateDCW(L"WINSPOOL", name, NULL, dm);
    }
    free(dm);
    ClosePrinter(h);
    return dc;
}

bool rubraview_pal_printer_sheet(u8str_t printer, int32_t paper_id, int32_t orientation, rubraview_print_sheet_t *out) {
    if (!out) return false;
    *out = (rubraview_print_sheet_t){0};
    HDC dc = printer_dc(printer, paper_id, orientation, 1);
    if (!dc) return false;
    *out = (rubraview_print_sheet_t){
        .paper_w = GetDeviceCaps(dc, PHYSICALWIDTH), .paper_h = GetDeviceCaps(dc, PHYSICALHEIGHT),
        .print_x = GetDeviceCaps(dc, PHYSICALOFFSETX), .print_y = GetDeviceCaps(dc, PHYSICALOFFSETY),
        .print_w = GetDeviceCaps(dc, HORZRES), .print_h = GetDeviceCaps(dc, VERTRES),
        .dpi_x = GetDeviceCaps(dc, LOGPIXELSX), .dpi_y = GetDeviceCaps(dc, LOGPIXELSY),
    };
    DeleteDC(dc);
    return out->paper_w > 0 && out->paper_h > 0 && out->print_w > 0 && out->print_h > 0 && out->dpi_x > 0 && out->dpi_y > 0;
}

bool rubraview_pal_print_pictures_on(u8str_t printer, int32_t paper_id, int32_t orientation, int32_t copies,
                                     const u8str_t *paths, size_t count, const rubraview_print_options_t *options,
                                     size_t *out_printed) {
    if (out_printed) *out_printed = 0;
    if (!paths || count == 0) return false;
    rubraview_print_options_t chosen = options ? *options : rubraview_print_options_default();
    rubraview_print_options_clamp(&chosen);
    HDC dc = printer_dc(printer, paper_id, orientation, copies);
    if (!dc) return false;
    size_t printed = 0;
    bool started = print_on_dc(dc, paths, count, &chosen, NULL, &printed);
    DeleteDC(dc);
    if (out_printed) *out_printed = printed;
    return started;
}


/* ---- D-154: the other copies on this machine ---- */

/* The MSI's UpgradeCode: resources/distribution/rubraview.toml's, which never changes. */
#define RUBRAVIEW_MSI_UPGRADE_CODE L"{C3CBE9C7-8E3F-4E2D-9E98-A28BD69E5714}"
#define PACKAGES_KEY L"Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\CurrentVersion\\AppModel\\Repository\\Packages"

static bool same_file(const WCHAR *a, const WCHAR *b) { return a[0] && b[0] && lstrcmpiW(a, b) == 0; }

/* The program the MSI's copy registered for every user's Windows 11 menu, if any. */
static bool msi_menu_program(WCHAR *out, DWORD cap) {
    WCHAR items[512];
    DWORD size = sizeof(items);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, MENU_CLASS_KEY, L"Items", RRF_RT_REG_MULTI_SZ, NULL, items, &size) != ERROR_SUCCESS || !items[0]) return false;
    WCHAR sub[600];
    wcscpy(sub, MENU_CLASS_KEY L"\\");
    wcsncat(sub, items, 64);
    size = cap * sizeof(WCHAR);
    return RegGetValueW(HKEY_LOCAL_MACHINE, sub, L"Command", RRF_RT_REG_SZ, NULL, out, &size) == ERROR_SUCCESS;
}

rubraview_other_copies_t rubraview_pal_shell_other_copies(void) {
    rubraview_other_copies_t other = {0};
    WCHAR exe[MAX_PATH * 2];
    if (!program_path(exe, MAX_PATH * 2, NULL)) exe[0] = 0;

    /* The MSI: installed if its UpgradeCode has a product; another copy
       unless its registered menu runs this very program. */
    HMODULE msi = LoadLibraryExW(L"msi.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (msi) {
        typedef UINT (WINAPI *related_fn)(LPCWSTR, DWORD, DWORD, LPWSTR);
        related_fn related = (related_fn)(void *)GetProcAddress(msi, "MsiEnumRelatedProductsW");
        WCHAR product[40];
        if (related && related(RUBRAVIEW_MSI_UPGRADE_CODE, 0, 0, product) == ERROR_SUCCESS) {
            WCHAR registered[MAX_PATH * 2];
            bool menu = msi_menu_program(registered, MAX_PATH * 2);
            WCHAR full[256];
            other.msi_menu = menu && get_string(HKEY_LOCAL_MACHINE, MENU_ROOT, PACKAGE_VALUE, full, 256);
            other.msi = !(menu && same_file(registered, exe));
            if (!menu) {
                /* Nothing registered to tell by: the MSI's copy is in Program Files. */
                WCHAR pf[MAX_PATH];
                DWORD n = GetEnvironmentVariableW(L"ProgramFiles", pf, MAX_PATH);
                other.msi = !(n > 0 && n < MAX_PATH && _wcsnicmp(exe, pf, n) == 0);
            }
        }
        FreeLibrary(msi);
    }

    /* The Store: a package of this user's with rubraview.exe in its folder
       (not the MSI's own menu package, and not this one). */
    HKEY packages = NULL;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, PACKAGES_KEY, 0, KEY_READ, &packages) == ERROR_SUCCESS) {
        static const WCHAR menu_package[] = WIDE(RUBRAVIEW_MENU_PACKAGE_NAME) L"_";
        WCHAR name[256];
        for (DWORD i = 0; !other.store; ++i) {
            DWORD len = 256;
            if (RegEnumKeyExW(packages, i, name, &len, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) break;
            if (_wcsnicmp(name, menu_package, wcslen(menu_package)) == 0) continue;
            WCHAR root[MAX_PATH + 32];
            DWORD size = MAX_PATH * sizeof(WCHAR);
            if (RegGetValueW(packages, name, L"PackageRootFolder", RRF_RT_REG_SZ, NULL, root, &size) != ERROR_SUCCESS) continue;
            size_t n = wcslen(root);
            if (n == 0 || n > MAX_PATH) continue;
            wcscpy(root + n, root[n - 1] == L'\\' ? L"rubraview.exe" : L"\\rubraview.exe");
            if (GetFileAttributesW(root) == INVALID_FILE_ATTRIBUTES) continue;
            if (same_file(root, exe)) continue;   /* this copy is that package */
            other.store = true;
        }
        RegCloseKey(packages);
    }
    return other;
}

#endif /* _WIN32 */
