/*
 * RV-279: the registry's verbs of a file, read in the viewer (read only: no COM, nothing loaded), run as their
 * command lines say; Explorer's own menus and the verbs only Windows can run, given to the child
 * (`--shell-menu-host`, pal_shellhost_win32.c).
 */
#ifdef _WIN32
#include "rubraview/pal/pal_shellverbs.h"
#include "rubraview/pal/pal_wide.h"

#include <windows.h>
#include <shlwapi.h>
#include <shellapi.h>

#define WKEY 700
#define WTEXT 2200

static bool to_wide(const char *text, WCHAR *out, int cap) {
    int n = MultiByteToWideChar(CP_UTF8, 0, text, -1, out, cap);
    if (n <= 0) { out[0] = 0; return false; }
    return true;
}

static bool to_utf8(const WCHAR *text, char *out, size_t cap) {
    int n = WideCharToMultiByte(CP_UTF8, 0, text, -1, out, (int)cap, NULL, NULL);
    if (n <= 0) { if (cap) out[0] = '\0'; return false; }
    return true;
}

static HKEY root_key(rubraview_shellreg_root_t root) {
    return root == RUBRAVIEW_SHELLREG_USER ? HKEY_CURRENT_USER : root == RUBRAVIEW_SHELLREG_MACHINE ? HKEY_LOCAL_MACHINE : HKEY_CLASSES_ROOT;
}

/* The last key opened for its subkeys, kept while the same key is walked. */
typedef struct reader {
    HKEY open;
    rubraview_shellreg_root_t open_root;
    char open_name[WKEY];
} reader_t;

static bool reg_value(void *ctx, rubraview_shellreg_root_t root, const char *key, const char *name, char *out, size_t cap) {
    (void)ctx;
    WCHAR wkey[WKEY], wname[200];
    if (!to_wide(key, wkey, WKEY) || !to_wide(name, wname, 200)) return false;
    HKEY k = NULL;
    if (RegOpenKeyExW(root_key(root), wkey, 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS) return false;
    static WCHAR data[WTEXT];
    DWORD type = 0, size = (DWORD)(sizeof data - sizeof(WCHAR));
    LONG got = RegQueryValueExW(k, wname[0] ? wname : NULL, NULL, &type, (BYTE *)data, &size);
    RegCloseKey(k);
    if (got != ERROR_SUCCESS) return false;
    if (type == REG_DWORD && size >= 4) {
        DWORD v = 0;
        proven_sys_mem_copy(&v, data, 4);
        return rubraview_fmt(out, cap, "{}", PROVEN_ARG((uint32_t)v)) >= 0;
    }
    if (type != REG_SZ && type != REG_EXPAND_SZ) { out[0] = '\0'; return true; }   /* there, as a mark */
    data[size / sizeof(WCHAR)] = 0;
    if (type == REG_EXPAND_SZ) {
        static WCHAR expanded[WTEXT];
        DWORD n = ExpandEnvironmentStringsW(data, expanded, WTEXT);
        if (n > 0 && n <= WTEXT) return to_utf8(expanded, out, cap);
    }
    return to_utf8(data, out, cap);
}

static bool reg_subkey(void *ctx, rubraview_shellreg_root_t root, const char *key, size_t index, char *out, size_t cap) {
    reader_t *r = (reader_t *)ctx;
    if (!r->open || r->open_root != root || rubraview_cstr_cmp(r->open_name, key) != 0) {
        if (r->open) RegCloseKey(r->open);
        r->open = NULL;
        WCHAR wkey[WKEY];
        if (!to_wide(key, wkey, WKEY)) return false;
        if (RegOpenKeyExW(root_key(root), wkey, 0, KEY_ENUMERATE_SUB_KEYS, &r->open) != ERROR_SUCCESS) { r->open = NULL; return false; }
        r->open_root = root;
        int n = rubraview_fmt(r->open_name, sizeof r->open_name, "{}", PROVEN_ARG(key));
        if (n < 0 || (size_t)n >= sizeof r->open_name) { RegCloseKey(r->open); r->open = NULL; return false; }
    }
    WCHAR name[256];
    DWORD len = 256;
    if (RegEnumKeyExW(r->open, (DWORD)index, name, &len, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) return false;
    return to_utf8(name, out, cap);
}

static bool reg_indirect(void *ctx, const char *text, char *out, size_t cap) {
    (void)ctx;
    WCHAR wtext[WTEXT], read[1024];
    if (!to_wide(text, wtext, WTEXT)) return false;
    if (FAILED(SHLoadIndirectString(wtext, read, 1024, NULL))) return false;
    return to_utf8(read, out, cap);
}

size_t rubraview_pal_shellverbs_list(const char *path, bool is_folder, bool shift, size_t files,
                                     rubraview_shellverb_t *out, size_t cap) {
    reader_t r = {0};
    rubraview_shellreg_t reg = { &r, reg_value, reg_subkey, reg_indirect };
    size_t n = rubraview_shellverbs_list(&reg, path, is_folder, shift, files, out, cap);
    if (r.open) RegCloseKey(r.open);
    return n;
}

/* ---- the child ---- */

/* The list file of a request: the temp folder's rubraview-menu-<pid>-<serial>.txt; its input and answer beside it. */
static bool list_path(uint32_t serial, const WCHAR *suffix, WCHAR *list, size_t cap) {
    WCHAR temp[MAX_PATH];
    if (GetTempPathW(MAX_PATH, temp) == 0) return false;
    char name8[96];
    if (rubraview_fmt(name8, sizeof name8, "rubraview-menu-{}-{}.txt", PROVEN_ARG((uint32_t)GetCurrentProcessId()), PROVEN_ARG(serial)) < 0) return false;
    WCHAR wname[96];
    return to_wide(name8, wname, 96) && rubraview_wcopy(list, cap, temp) && rubraview_wcat(list, cap, wname) &&
           (!suffix || rubraview_wcat(list, cap, suffix));
}

static bool write_whole(const WCHAR *path, const void *bytes, size_t n) {
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, NULL);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD wrote = 0;
    bool ok = n == 0 || (WriteFile(f, bytes, (DWORD)n, &wrote, NULL) && wrote == (DWORD)n);
    CloseHandle(f);
    return ok;
}

static bool start_child_with(const WCHAR *kind, const char *const *paths, size_t count, int32_t x, int32_t y,
                             proven_u8str_view_t input, uint32_t *out_serial) {
    WCHAR exe[MAX_PATH * 2], list[MAX_PATH * 2];
    DWORD got = GetModuleFileNameW(NULL, exe, MAX_PATH * 2);
    if (got == 0 || got >= MAX_PATH * 2) return false;
    static LONG serial = 0;
    uint32_t mine = (uint32_t)InterlockedIncrement(&serial);
    if (out_serial) *out_serial = mine;
    if (!list_path(mine, NULL, list, MAX_PATH * 2)) return false;
    if (input.size > 0) {
        WCHAR in[MAX_PATH * 2];
        if (!list_path(mine, L".in", in, MAX_PATH * 2) || !write_whole(in, input.ptr, input.size)) return false;
    }
    /* The files, one a line (rubraview_shellreq_split_lines reads it back) */
    HANDLE f = CreateFileW(list, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, NULL);
    if (f == INVALID_HANDLE_VALUE) return false;
    bool wrote = true;
    for (size_t i = 0; i < count && wrote; ++i) {
        DWORD n = 0, len = (DWORD)proven_cstr_len(paths[i]);
        wrote = WriteFile(f, paths[i], len, &n, NULL) && n == len && WriteFile(f, "\n", 1, &n, NULL) && n == 1;
    }
    CloseHandle(f);
    if (!wrote) { DeleteFileW(list); return false; }
    static WCHAR line[MAX_PATH * 6];
    char xy[48];
    WCHAR wxy[48];
    if (rubraview_fmt(xy, sizeof xy, " {} {}", PROVEN_ARG(x), PROVEN_ARG(y)) < 0 || !to_wide(xy, wxy, 48)) { DeleteFileW(list); return false; }
    line[0] = 0;
    bool fits = rubraview_wcat(line, MAX_PATH * 6, L"\"") && rubraview_wcat(line, MAX_PATH * 6, exe) &&
                rubraview_wcat(line, MAX_PATH * 6, L"\" --shell-menu-host \"") && rubraview_wcat(line, MAX_PATH * 6, kind) &&
                rubraview_wcat(line, MAX_PATH * 6, L"\" \"") && rubraview_wcat(line, MAX_PATH * 6, list) &&
                rubraview_wcat(line, MAX_PATH * 6, L"\"") && rubraview_wcat(line, MAX_PATH * 6, wxy);
    if (!fits) { DeleteFileW(list); return false; }
    STARTUPINFOW si = { .cb = sizeof si };
    PROCESS_INFORMATION pi = {0};
    if (!CreateProcessW(exe, line, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) { DeleteFileW(list); return false; }
    AllowSetForegroundWindow(pi.dwProcessId);   /* its menu must take the foreground, or a click elsewhere leaves it open */
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);   /* not waited for: the viewer forgets it */
    return true;
}

/* RV-281 phase 2: the same request, the child elevated through ShellExecuteEx's "runas" (UAC asks). */
static bool start_elevated(const WCHAR *kind, const char *const *paths, size_t count, proven_u8str_view_t input, uint32_t *out_serial,
                           HWND owner) {
    WCHAR exe[MAX_PATH * 2], list[MAX_PATH * 2];
    DWORD got = GetModuleFileNameW(NULL, exe, MAX_PATH * 2);
    if (got == 0 || got >= MAX_PATH * 2) return false;
    static LONG serial = 1000000;   /* apart from start_child_with's */
    uint32_t mine = (uint32_t)InterlockedIncrement(&serial);
    if (out_serial) *out_serial = mine;
    if (!list_path(mine, NULL, list, MAX_PATH * 2)) return false;
    if (input.size > 0) {
        WCHAR in[MAX_PATH * 2];
        if (!list_path(mine, L".in", in, MAX_PATH * 2) || !write_whole(in, input.ptr, input.size)) return false;
    }
    size_t total = 0;
    for (size_t i = 0; i < count; ++i) total += proven_cstr_len(paths[i]) + 1;
    char *all = (char *)rubraview_alloc(total + 1);
    if (!all) return false;
    size_t at = 0;
    for (size_t i = 0; i < count; ++i) {
        size_t n = proven_cstr_len(paths[i]);
        proven_sys_mem_copy(all + at, paths[i], n);
        at += n;
        all[at++] = '\n';
    }
    bool wrote = write_whole(list, all, at);
    rubraview_free(all);
    if (!wrote) return false;
    static WCHAR args[MAX_PATH * 6];
    args[0] = 0;
    if (!(rubraview_wcat(args, MAX_PATH * 6, L"--shell-menu-host \"") && rubraview_wcat(args, MAX_PATH * 6, kind) &&
          rubraview_wcat(args, MAX_PATH * 6, L"\" \"") && rubraview_wcat(args, MAX_PATH * 6, list) && rubraview_wcat(args, MAX_PATH * 6, L"\" 0 0"))) {
        DeleteFileW(list);
        return false;
    }
    SHELLEXECUTEINFOW sei = { .cbSize = sizeof sei, .fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI, .hwnd = owner, .lpVerb = L"runas",
                              .lpFile = exe, .lpParameters = args, .nShow = SW_HIDE };
    if (!ShellExecuteExW(&sei)) { DeleteFileW(list); return false; }   /* said no to Windows' question */
    return true;
}

bool rubraview_pal_shellhost_ask_elevated(const char *kind, const char *const *paths, size_t count, proven_u8str_view_t input,
                                          uint32_t *ticket, void *native_window) {
    WCHAR wkind[64];
    if (!kind || !paths || count == 0 || !to_wide(kind, wkind, 64)) return false;
    return start_elevated(wkind, paths, count, input, ticket, (HWND)native_window);
}

static bool start_child(const WCHAR *kind, const char *const *paths, size_t count, int32_t x, int32_t y) {
    return start_child_with(kind, paths, count, x, y, (proven_u8str_view_t){0}, NULL);
}

bool rubraview_pal_shellhost_ask(const char *kind, const char *const *paths, size_t count, proven_u8str_view_t input,
                                 uint32_t *ticket) {
    WCHAR wkind[64];
    if (!kind || !paths || count == 0 || !to_wide(kind, wkind, 64)) return false;
    return start_child_with(wkind, paths, count, 0, 0, input, ticket);
}

int rubraview_pal_shellhost_answer(uint32_t ticket, char **text, size_t *length) {
    WCHAR out[MAX_PATH * 2];
    *text = NULL;
    *length = 0;
    if (!list_path(ticket, L".out", out, MAX_PATH * 2)) return 0;
    HANDLE f = CreateFileW(out, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    LARGE_INTEGER size = {0};
    char *buf = NULL;
    DWORD got = 0;
    if (GetFileSizeEx(f, &size) && size.QuadPart >= 0 && size.QuadPart < 16 * 1024 * 1024 &&
        (buf = (char *)rubraview_alloc((size_t)size.QuadPart + 1)) != NULL &&
        (size.QuadPart == 0 || ReadFile(f, buf, (DWORD)size.QuadPart, &got, NULL))) {
        buf[got] = '\0';
    }
    CloseHandle(f);
    DeleteFileW(out);
    if (!buf) return 0;
    *text = buf;
    *length = got;
    return 1;
}

void rubraview_pal_shellhost_forget(uint32_t ticket) {
    WCHAR p[MAX_PATH * 2];
    if (list_path(ticket, L".out", p, MAX_PATH * 2)) DeleteFileW(p);
    if (list_path(ticket, L".in", p, MAX_PATH * 2)) DeleteFileW(p);
}

bool rubraview_pal_shellhost_show(const char *kind, const char *const *paths, size_t count, void *native_window) {
    WCHAR wkind[64];
    if (!kind || !paths || count == 0 || !to_wide(kind, wkind, 64)) return false;
    POINT at = {0};
    RECT r;
    if (native_window && GetWindowRect((HWND)native_window, &r)) { at.x = r.left + 40; at.y = r.top + 40; } else GetCursorPos(&at);
    return start_child(wkind, paths, count, (int32_t)at.x, (int32_t)at.y);
}

bool rubraview_pal_shellmenu_show(rubraview_shellmenu_kind_t kind, const char *const *paths, size_t count,
                                  void *native_window, double x, double y) {
    if (!paths || count == 0) return false;
    POINT at = { (LONG)x, (LONG)y };
    if (!native_window || !ClientToScreen((HWND)native_window, &at)) GetCursorPos(&at);
    const WCHAR *word = kind == RUBRAVIEW_SHELLMENU_WIN11 ? L"win11" : kind == RUBRAVIEW_SHELLMENU_CLASSIC_EXTENDED ? L"classic-shift" : L"classic";
    return start_child(word, paths, count, (int32_t)at.x, (int32_t)at.y);
}

bool rubraview_pal_shellmenu_shift_held(void) { return (GetKeyState(VK_SHIFT) & 0x8000) != 0; }

/* ---- a verb run ---- */

static bool run_line(const char *command, const char *path) {
    static char line8[4200];
    static WCHAR line[4200], folder[MAX_PATH * 2];
    if (!rubraview_shellverb_command(command, path, line8, sizeof line8) || !to_wide(line8, line, 4200)) return false;
    if (!to_wide(path, folder, MAX_PATH * 2)) return false;
    WCHAR *cut = rubraview_wrchr(folder, L'\\');
    if (cut && cut > folder + 2) *cut = 0; else if (cut) cut[1] = 0;
    STARTUPINFOW si = { .cb = sizeof si };
    PROCESS_INFORMATION pi = {0};
    DWORD attrs = GetFileAttributesW(folder);
    bool folder_ok = attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY);
    if (!CreateProcessW(NULL, line, NULL, NULL, FALSE, 0, NULL, folder_ok ? folder : NULL, &si, &pi)) return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

bool rubraview_pal_shellverbs_run(const rubraview_shellverb_t *verb, const char *const *paths, size_t count) {
    if (!verb || !paths || count == 0) return false;
    if (verb->by_shell) {
        WCHAR kind[120];
        char k8[120];
        if (rubraview_fmt(k8, sizeof k8, "verb:{}", PROVEN_ARG(verb->name)) >= (int)sizeof k8 || !to_wide(k8, kind, 120)) return false;
        POINT at = {0};
        GetCursorPos(&at);
        return start_child(kind, paths, count, (int32_t)at.x, (int32_t)at.y);
    }
    bool all = true;
    for (size_t i = 0; i < count; ++i) all = run_line(verb->command, paths[i]) && all;
    return all;
}

#endif /* _WIN32 */
