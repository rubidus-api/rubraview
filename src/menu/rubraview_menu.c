// src/menu/rubraview_menu.c - Rubraview's items in Explorer's Windows 11 context menu (D-86).
//
// Adapted from Rubrapack's src/menu/rubrapack_menu.c (MIT, Copyright (c) 2026 rubidus-api; see
// vendor/rubrapack/LICENSE): the same COM server, reading Rubraview's own registry key, with two
// additions - an item is hidden on file types the reader did not choose (the On value), and a
// selection too long for a command line is handed over as a file of paths.
//
// Windows 11 shows in its context menu only the commands a package with identity declares
// (desktop4 fileExplorerContextMenus, each a COM class). rubraview_menu.msix, registered with the
// program's folder as its external location, names this DLL as the class's server
// (com:SurrogateServer: it runs in dllhost.exe, never inside Explorer). What the class shows and
// starts is read from the registry, where the viewer's registration wrote it:
//
//   HKLM (or HKCU) \Software\Rubraview\Menu\{CLSID}
//     Text        the item's text;  Text.<lang> for a language ("ko"; the user's display language
//                 picks one, Text is the fallback)
//     Icon        "path" or "path,index" (optional)
//     Command     the program, a full path
//     Args        its arguments: %1 = the selected path, put in as it is; %* = every selected
//                 path, each quoted here. Without either, the paths are appended, quoted.
//     Multi       "each" (one run per selected item), "one" (one run with all of them),
//                 "single" (the item shows only when one thing is selected)
//     On          ";jpg;png;": the extensions the item shows on (every selected file must have
//                 one of them); absent = on anything
//     Items       REG_MULTI_SZ: the names of sub keys, each a sub-item with the same values
//                 (then this key has no Command: it is a sub-menu)
//
// Nothing here is read from the selected files, and nothing is run but the Command the
// registration wrote (HKLM needs an administrator to change; HKCU is the user's own).
// Windows only; needs the system DLLs and proven_c_lib's static library: its memory, text and
// formatting go proven's ways as the viewer's do (RV-220, D-276).

#define COBJMACROS
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shlwapi.h>
#include "rubraview/pal/pal_wide.h"

enum { TEXT_CAP = 1024, PATH_CAP = 32768, MAX_ITEMS = 64, MAX_RUNS = 8192, MAX_LOOKED_AT = 256 };

static LONG live;                   // objects and locks: DllCanUnloadNow
static HMODULE self;

#define MENU_KEY L"Software\\Rubraview\\Menu\\"

// ---- memory and text, proven's ways (D-276) ----------------------------------------------------

// The DLL's blocks: proven's allocator trait over the process heap, zeroed. They never leave this DLL.
static proven_result_mem_mut_t heap_alloc(void *ctx, proven_size_t size, proven_size_t align) {
    (void)ctx;
    if (size == 0 || align > (proven_size_t)alignof(max_align_t)) return (proven_result_mem_mut_t){ .err = PROVEN_ERR_INVALID_ARG };
    void *p = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (SIZE_T)size);
    return p ? (proven_result_mem_mut_t){ .err = PROVEN_OK, .value = { .ptr = p, .size = size } }
             : (proven_result_mem_mut_t){ .err = PROVEN_ERR_NOMEM };
}

static proven_result_mem_mut_t heap_realloc(void *ctx, void *old_ptr, proven_size_t old_size, proven_size_t new_size, proven_size_t align) {
    (void)old_size;
    if (old_ptr == NULL) return heap_alloc(ctx, new_size, align);
    if (new_size == 0) {
        HeapFree(GetProcessHeap(), 0, old_ptr);
        return (proven_result_mem_mut_t){ .err = PROVEN_OK };
    }
    if (align > (proven_size_t)alignof(max_align_t)) return (proven_result_mem_mut_t){ .err = PROVEN_ERR_INVALID_ARG };
    void *p = HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, old_ptr, (SIZE_T)new_size);
    return p ? (proven_result_mem_mut_t){ .err = PROVEN_OK, .value = { .ptr = p, .size = new_size } }
             : (proven_result_mem_mut_t){ .err = PROVEN_ERR_NOMEM };
}

static void heap_free(void *ctx, void *ptr) {
    (void)ctx;
    if (ptr) HeapFree(GetProcessHeap(), 0, ptr);
}

static const proven_allocator_t heap = { .ctx = NULL, .alloc_fn = heap_alloc, .realloc_fn = heap_realloc, .free_fn = heap_free };

static void *block_new(proven_size_t bytes) {
    proven_result_mem_mut_t r = heap.alloc_fn(heap.ctx, bytes, (proven_size_t)alignof(max_align_t));
    return r.err == PROVEN_OK ? r.value.ptr : NULL;
}

static void block_free(void *block) { heap.free_fn(heap.ctx, block); }

// A proven format ({} placeholders) written into `dst` of `cap` units as UTF-16; false (dst empty)
// when it did not fit.
static bool wide_from(wchar_t *dst, size_t cap, const char *utf8, proven_fmt_result_t r) {
    proven_size_t written = 0;
    dst[0] = 0;
    if (r.err != PROVEN_OK || proven_utf8_to_utf16(rubraview_bytes_view(utf8, r.written), (proven_u16 *)dst, cap - 1, &written) != PROVEN_OK) {
        dst[0] = 0;
        return false;
    }
    dst[written] = 0;
    return true;
}

// A wide literal as a view.
#define WIDE(s) ((proven_u16str_view_t){ .ptr = (const proven_u16 *)(s), .size = sizeof(s) / sizeof(wchar_t) - 1 })

// Text through the log file: the units of a view, as they are.
static void write_wide(HANDLE f, proven_u16str_view_t text) {
    DWORD put = 0;
    WriteFile(f, text.ptr, (DWORD)(text.size * sizeof(wchar_t)), &put, NULL);
}

// With the value HKCU\Software\Rubraview\MenuLog (a file's path; set by hand, never by the viewer)
// each start of a program is appended to that file: the command line and whether it started.
static void say(const wchar_t *what, const wchar_t *text, DWORD code) {
    wchar_t path[MAX_PATH];
    DWORD type = 0, size = sizeof path - sizeof(wchar_t);
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Rubraview", 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS) return;
    LONG e = RegQueryValueExW(k, L"MenuLog", NULL, &type, (BYTE *)path, &size);
    RegCloseKey(k);
    if (e != ERROR_SUCCESS || type != REG_SZ || size < 2 * sizeof(wchar_t)) return;
    path[size / sizeof(wchar_t)] = 0;
    HANDLE f = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    char utf8[32];
    wchar_t num[32];
    (void)wide_from(num, RUBRAVIEW_COUNT_OF(num), utf8, proven_fmt_to(utf8, sizeof utf8, " ({})\r\n", PROVEN_ARG((uint32_t)code)));
    write_wide(f, rubraview_wide_view(what));
    write_wide(f, rubraview_wide_view(text));
    write_wide(f, rubraview_wide_view(num));
    CloseHandle(f);
}

// ---- the registry ----------------------------------------------------------------------------

// Opens <root>\Software\Rubraview\Menu\<path>, HKLM first, then HKCU.
static bool open_item(const wchar_t *path, HKEY *out) {
    wchar_t key[512];
    if (!rubraview_wcopy_a(key, MENU_KEY) || !rubraview_wcat_a(key, path)) return false;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, key, 0, KEY_QUERY_VALUE, out) == ERROR_SUCCESS) return true;
    return RegOpenKeyExW(HKEY_CURRENT_USER, key, 0, KEY_QUERY_VALUE, out) == ERROR_SUCCESS;
}

// A string value (REG_SZ or REG_EXPAND_SZ, taken as written), NUL-terminated; false when absent or too long.
static bool value(HKEY k, const wchar_t *name, wchar_t *out, DWORD cap) {
    DWORD type = 0, size = (cap - 1) * sizeof(wchar_t);
    if (RegQueryValueExW(k, name, NULL, &type, (BYTE *)out, &size) != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) return false;
    out[size / sizeof(wchar_t)] = 0;
    return true;
}

// The language code of the user's display language, as the sources write it ("ko", "pt-br", ...).
static void ui_language(wchar_t out[LOCALE_NAME_MAX_LENGTH]) {
    out[0] = 0;
    ULONG n = 0, cap = 0;
    if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &n, NULL, &cap) && cap > 1) {
        wchar_t *list = block_new(cap * sizeof *list);
        if (list && GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &n, list, &cap) && rubraview_wlen(list) < LOCALE_NAME_MAX_LENGTH)
            (void)rubraview_wcopy(out, LOCALE_NAME_MAX_LENGTH, list);
        block_free(list);
    }
    CharLowerW(out);
}

// Text.<language>, then Text.<language without its region>, then Text.
static bool item_text(HKEY k, wchar_t *out, DWORD cap) {
    wchar_t lang[LOCALE_NAME_MAX_LENGTH], name[LOCALE_NAME_MAX_LENGTH + 8];
    ui_language(lang);
    for (int pass = 0; pass < 2 && lang[0]; ++pass) {
        if (rubraview_wcopy_a(name, L"Text.") && rubraview_wcat_a(name, lang) && value(k, name, out, cap)) return true;
        wchar_t *dash = rubraview_wchr(lang, L'-');
        if (dash == NULL) break;
        *dash = 0;
    }
    return value(k, L"Text", out, cap);
}

// A path as the registry gives it.
static bool resolve(const wchar_t *in, wchar_t *out, size_t cap) {
    if (rubraview_wlen(in) >= cap) return false;
    return rubraview_wcopy(out, cap, in);
}

// Does every selected file have one of the extensions in `on` (";jpg;png;")? Only so many are
// looked at: a selection of thousands is not walked each time the menu opens.
static bool selection_is_on(const wchar_t *on, IShellItemArray *items) {
    DWORD count = 0;
    if (items == NULL || FAILED(IShellItemArray_GetCount(items, &count)) || count == 0) return false;
    if (count > MAX_LOOKED_AT) count = MAX_LOOKED_AT;
    for (DWORD i = 0; i < count; ++i) {
        IShellItem *si = NULL;
        if (FAILED(IShellItemArray_GetItemAt(items, i, &si))) return false;
        wchar_t *name = NULL;
        bool found = false;
        if (SUCCEEDED(IShellItem_GetDisplayName(si, SIGDN_FILESYSPATH, &name)) && name) {
            const wchar_t *dot = rubraview_wrchr(name, L'.'), *slash = rubraview_wrchr(name, L'\\');
            wchar_t want[40];
            if (dot && (slash == NULL || dot > slash) && rubraview_wlen(dot + 1) > 0 && rubraview_wlen(dot + 1) < 32 &&
                rubraview_wcopy_a(want, L";") && rubraview_wcat_a(want, dot + 1) && rubraview_wcat_a(want, L";")) {
                CharLowerW(want);
                found = rubraview_wstr(on, want) != NULL;
            }
            CoTaskMemFree(name);
        }
        IShellItem_Release(si);
        if (!found) return false;
    }
    return true;
}

// ---- the command ------------------------------------------------------------------------------

typedef struct {
    IExplorerCommand     cmd;
    IEnumExplorerCommand en;        // the same object enumerates its own sub-items
    LONG                 refs;
    wchar_t              path[400]; // below Menu\: "{CLSID}" or "{CLSID}\<sub item>"
    wchar_t              subs[MAX_ITEMS][64];
    ULONG                nsubs, at;
} item_t;

static item_t *item_new(const wchar_t *path);

static item_t *from_cmd(IExplorerCommand *p) { return (item_t *)((char *)p - offsetof(item_t, cmd)); }
static item_t *from_en(IEnumExplorerCommand *p) { return (item_t *)((char *)p - offsetof(item_t, en)); }

static HRESULT item_query(item_t *it, REFIID riid, void **out) {
    if (out == NULL) return E_POINTER;
    *out = NULL;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IExplorerCommand)) *out = &it->cmd;
    else if (IsEqualIID(riid, &IID_IEnumExplorerCommand)) *out = &it->en;
    else return E_NOINTERFACE;
    InterlockedIncrement(&it->refs);
    return S_OK;
}

static ULONG item_release(item_t *it) {
    LONG n = InterlockedDecrement(&it->refs);
    if (n == 0) {
        block_free(it);
        InterlockedDecrement(&live);
    }
    return (ULONG)n;
}

static HRESULT STDMETHODCALLTYPE cmd_query(IExplorerCommand *p, REFIID riid, void **out) { return item_query(from_cmd(p), riid, out); }
static ULONG STDMETHODCALLTYPE cmd_addref(IExplorerCommand *p) { return (ULONG)InterlockedIncrement(&from_cmd(p)->refs); }
static ULONG STDMETHODCALLTYPE cmd_release(IExplorerCommand *p) { return item_release(from_cmd(p)); }

static HRESULT STDMETHODCALLTYPE cmd_title(IExplorerCommand *p, IShellItemArray *items, LPWSTR *out) {
    (void)items;
    if (out == NULL) return E_POINTER;
    *out = NULL;
    HKEY k;
    if (!open_item(from_cmd(p)->path, &k)) return E_FAIL;
    wchar_t text[TEXT_CAP];
    bool ok = item_text(k, text, TEXT_CAP);
    RegCloseKey(k);
    return ok ? SHStrDupW(text, out) : E_FAIL;
}

static HRESULT STDMETHODCALLTYPE cmd_icon(IExplorerCommand *p, IShellItemArray *items, LPWSTR *out) {
    (void)items;
    if (out == NULL) return E_POINTER;
    *out = NULL;
    HKEY k;
    if (!open_item(from_cmd(p)->path, &k)) return E_FAIL;
    wchar_t *mine = block_new(2 * PATH_CAP * sizeof *mine);
    HRESULT hr = E_NOTIMPL;
    if (mine && value(k, L"Icon", mine, PATH_CAP) && mine[0] && resolve(mine, mine + PATH_CAP, PATH_CAP)) hr = SHStrDupW(mine + PATH_CAP, out);
    block_free(mine);
    RegCloseKey(k);
    return hr;
}

static HRESULT STDMETHODCALLTYPE cmd_tip(IExplorerCommand *p, IShellItemArray *items, LPWSTR *out) {
    (void)p;
    (void)items;
    if (out) *out = NULL;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE cmd_name(IExplorerCommand *p, GUID *out) {
    (void)p;
    if (out) *out = GUID_NULL;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE cmd_state(IExplorerCommand *p, IShellItemArray *items, BOOL slow, EXPCMDSTATE *out) {
    (void)slow;
    if (out == NULL) return E_POINTER;
    *out = ECS_ENABLED;
    HKEY k;
    if (!open_item(from_cmd(p)->path, &k)) {
        *out = ECS_HIDDEN;
        return S_OK;
    }
    wchar_t multi[16], on[TEXT_CAP];
    DWORD count = 0;
    if (value(k, L"Multi", multi, 16) && rubraview_wcmp(multi, L"single") == 0 && items && SUCCEEDED(IShellItemArray_GetCount(items, &count)) && count > 1)
        *out = ECS_HIDDEN;
    if (value(k, L"On", on, TEXT_CAP) && !selection_is_on(on, items)) *out = ECS_HIDDEN;
    RegCloseKey(k);
    return S_OK;
}

// Appends s to a growing command line; false when it does not fit.
static bool put(wchar_t *line, size_t *len, proven_u16str_view_t s) {
    if (*len + s.size + 1 > PATH_CAP) return false;
    proven_sys_mem_copy(line + *len, s.ptr, s.size * sizeof(wchar_t));
    *len += s.size;
    line[*len] = 0;
    return true;
}

// A path as one argument for CommandLineToArgvW's rules: quoted, a trailing backslash doubled.
static bool put_quoted(wchar_t *line, size_t *len, const wchar_t *path) {
    proven_u16str_view_t p = rubraview_wide_view(path);
    if (rubraview_wchr(path, L'"')) return false;       // not a file system path
    return put(line, len, WIDE(L"\"")) && put(line, len, p) && (p.size == 0 || path[p.size - 1] != L'\\' || put(line, len, WIDE(L"\\"))) &&
           put(line, len, WIDE(L"\""));
}

// The paths as a file in the user's temporary folder, one a line in UTF-8; its name in `out`.
static bool write_list(wchar_t **paths, DWORD count, wchar_t *out, size_t cap) {
    wchar_t dir[MAX_PATH + 1];
    DWORD n = GetTempPathW(MAX_PATH + 1, dir);
    if (n == 0 || n > MAX_PATH || n + 64 >= cap) return false;
    char name[MAX_PATH * 3 + 80];
    if (!wide_from(out, cap, name, proven_fmt_to(name, sizeof name, "{}rubraview-selection-{}-{}.txt", PROVEN_ARG(rubraview_wide_view(dir)),
                                                 PROVEN_ARG((uint32_t)GetCurrentProcessId()), PROVEN_ARG((uint32_t)GetTickCount()))))
        return false;
    HANDLE f = CreateFileW(out, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, NULL);
    if (f == INVALID_HANDLE_VALUE) return false;
    char *utf8 = block_new(PATH_CAP * 3 + 2);
    bool ok = utf8 != NULL;
    for (DWORD i = 0; ok && i < count; ++i) {
        proven_u16str_view_t path = rubraview_wide_view(paths[i]);
        proven_size_t m = 0;
        DWORD put_bytes = 0;
        if (path.size == 0 || proven_utf16_to_utf8(path.ptr, path.size, (proven_byte_t *)utf8, PATH_CAP * 3, &m) != PROVEN_OK || m == 0) continue;
        utf8[m++] = '\n';
        ok = WriteFile(f, utf8, (DWORD)m, &put_bytes, NULL) && put_bytes == (DWORD)m;
    }
    block_free(utf8);
    CloseHandle(f);
    if (!ok) DeleteFileW(out);
    return ok;
}

// Starts the program once with paths[0..count): args with %1 (the first) and %* (all of them).
// `list`, when given, stands in for %*: the paths are in that file (the program removes it).
static bool run_with(const wchar_t *program, const wchar_t *args, wchar_t **paths, DWORD count, const wchar_t *list);

static void run(const wchar_t *program, const wchar_t *args, wchar_t **paths, DWORD count) {
    if (run_with(program, args, paths, count, NULL) || count < 2) return;
    // Too long for a command line: the same, with the paths in a file.
    wchar_t list[MAX_PATH + 80];
    if (!write_list(paths, count, list, MAX_PATH + 80)) return;
    if (!run_with(program, args, paths, count, list)) DeleteFileW(list);
}

static bool run_with(const wchar_t *program, const wchar_t *args, wchar_t **paths, DWORD count, const wchar_t *list) {
    wchar_t *line = block_new(PATH_CAP * sizeof *line);
    if (line == NULL) return false;
    size_t len = 0;
    bool ok = put_quoted(line, &len, program), used = false, started = false;
    if (ok && args[0]) ok = put(line, &len, WIDE(L" "));
    for (const wchar_t *a = args; ok && *a; ++a) {
        if (a[0] == L'%' && (a[1] == L'1' || a[1] == L'V' || a[1] == L'v')) {
            // As written; before a closing quote a path's last backslash is doubled ("C:\" would
            // otherwise take the quote with it).
            proven_u16str_view_t first = rubraview_wide_view(count ? paths[0] : L"");
            ok = count == 0 || (!rubraview_wchr(paths[0], L'"') && put(line, &len, first) &&
                                (first.size == 0 || paths[0][first.size - 1] != L'\\' || a[2] != L'"' || put(line, &len, WIDE(L"\\"))));
            used = true;
            ++a;
        } else if (a[0] == L'%' && a[1] == L'*' && list) {
            ok = put(line, &len, WIDE(L"--paths-temp --paths-from=")) && put_quoted(line, &len, list);
            used = true;
            ++a;
        } else if (a[0] == L'%' && a[1] == L'*') {
            for (DWORD i = 0; ok && i < count; ++i) ok = (i == 0 || put(line, &len, WIDE(L" "))) && put_quoted(line, &len, paths[i]);
            used = true;
            ++a;
        } else {
            ok = put(line, &len, (proven_u16str_view_t){ .ptr = (const proven_u16 *)a, .size = 1 });
        }
    }
    for (DWORD i = 0; ok && !used && i < count; ++i) ok = put(line, &len, WIDE(L" ")) && put_quoted(line, &len, paths[i]);
    if (ok) {
        // In the selected item's folder, or the folder itself.
        wchar_t *dir = NULL;
        if (count && (dir = block_new(PATH_CAP * sizeof *dir)) != NULL) {
            (void)rubraview_wcopy(dir, PATH_CAP, paths[0]);
            DWORD a = GetFileAttributesW(dir);
            if (a == INVALID_FILE_ATTRIBUTES || !(a & FILE_ATTRIBUTE_DIRECTORY)) {
                wchar_t *slash = rubraview_wrchr(dir, L'\\');
                if (slash && slash > dir + 2) *slash = 0;
                else if (slash) slash[1] = 0;
            }
        }
        STARTUPINFOW si = { .cb = sizeof si };
        PROCESS_INFORMATION pi;
        if (CreateProcessW(program, line, NULL, NULL, FALSE, 0, NULL, dir, &si, &pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            started = true;
            say(L"started: ", line, 0);
        } else {
            say(L"not started: ", line, GetLastError());
        }
        block_free(dir);
    }
    block_free(line);
    return started;
}

static HRESULT STDMETHODCALLTYPE cmd_invoke(IExplorerCommand *p, IShellItemArray *items, IBindCtx *ctx) {
    (void)ctx;
    HKEY k;
    say(L"chosen: ", from_cmd(p)->path, 0);
    if (!open_item(from_cmd(p)->path, &k)) return E_FAIL;
    wchar_t *buf = block_new(3 * PATH_CAP * sizeof *buf);
    wchar_t multi[16] = L"each";
    HRESULT hr = E_FAIL;
    if (buf && value(k, L"Command", buf, PATH_CAP) && buf[0] && resolve(buf, buf + PATH_CAP, PATH_CAP)) {
        wchar_t *program = buf + PATH_CAP, *args = buf + 2 * PATH_CAP;
        if (!value(k, L"Args", args, PATH_CAP)) args[0] = 0;
        (void)value(k, L"Multi", multi, 16);
        // The selection as file system paths (what is not in the file system is left out).
        wchar_t **paths = block_new(MAX_RUNS * sizeof *paths);
        DWORD count = 0, n = 0;
        if (items && paths) (void)IShellItemArray_GetCount(items, &count);
        for (DWORD i = 0; i < count && n < MAX_RUNS; ++i) {
            IShellItem *si = NULL;
            if (FAILED(IShellItemArray_GetItemAt(items, i, &si))) continue;
            wchar_t *path = NULL;
            if (SUCCEEDED(IShellItem_GetDisplayName(si, SIGDN_FILESYSPATH, &path)) && path) paths[n++] = path;
            IShellItem_Release(si);
        }
        if (paths && rubraview_wcmp(multi, L"each") == 0 && n > 1) {
            for (DWORD i = 0; i < n; ++i) run(program, args, &paths[i], 1);
        } else if (paths) {
            run(program, args, paths, n);
        }
        for (DWORD i = 0; i < n; ++i) CoTaskMemFree(paths[i]);
        block_free(paths);
        hr = S_OK;
    } else {
        say(L"no usable Command for ", from_cmd(p)->path, GetLastError());
    }
    block_free(buf);
    RegCloseKey(k);
    return hr;
}

static HRESULT STDMETHODCALLTYPE cmd_flags(IExplorerCommand *p, EXPCMDFLAGS *out) {
    if (out == NULL) return E_POINTER;
    *out = from_cmd(p)->nsubs ? ECF_HASSUBCOMMANDS : ECF_DEFAULT;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE cmd_subs(IExplorerCommand *p, IEnumExplorerCommand **out) {
    if (out == NULL) return E_POINTER;
    *out = NULL;
    item_t *it = from_cmd(p);
    if (it->nsubs == 0) return E_NOTIMPL;
    item_t *e = item_new(it->path);         // an enumerator of its own, at the start
    if (e == NULL) return E_OUTOFMEMORY;
    *out = &e->en;
    return S_OK;
}

static const IExplorerCommandVtbl cmd_vtbl = { cmd_query, cmd_addref, cmd_release, cmd_title, cmd_icon, cmd_tip,
                                               cmd_name,  cmd_state,  cmd_invoke,  cmd_flags, cmd_subs };

static HRESULT STDMETHODCALLTYPE en_query(IEnumExplorerCommand *p, REFIID riid, void **out) { return item_query(from_en(p), riid, out); }
static ULONG STDMETHODCALLTYPE en_addref(IEnumExplorerCommand *p) { return (ULONG)InterlockedIncrement(&from_en(p)->refs); }
static ULONG STDMETHODCALLTYPE en_release(IEnumExplorerCommand *p) { return item_release(from_en(p)); }

static HRESULT STDMETHODCALLTYPE en_next(IEnumExplorerCommand *p, ULONG want, IExplorerCommand **out, ULONG *got) {
    item_t *it = from_en(p);
    ULONG n = 0;
    if (out == NULL) return E_POINTER;
    while (n < want && it->at < it->nsubs) {
        wchar_t path[400];
        const wchar_t *sub = it->subs[it->at++];
        if (!rubraview_wcopy_a(path, it->path) || !rubraview_wcat_a(path, L"\\") || !rubraview_wcat_a(path, sub)) continue;
        item_t *child = item_new(path);
        if (child == NULL) continue;
        out[n++] = &child->cmd;
    }
    if (got) *got = n;
    return n == want ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE en_skip(IEnumExplorerCommand *p, ULONG n) {
    item_t *it = from_en(p);
    it->at = it->at + n > it->nsubs ? it->nsubs : it->at + n;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE en_reset(IEnumExplorerCommand *p) {
    from_en(p)->at = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE en_clone(IEnumExplorerCommand *p, IEnumExplorerCommand **out) {
    if (out == NULL) return E_POINTER;
    item_t *e = item_new(from_en(p)->path);
    *out = e ? &e->en : NULL;
    if (e) e->at = from_en(p)->at;
    return e ? S_OK : E_OUTOFMEMORY;
}

static const IEnumExplorerCommandVtbl en_vtbl = { en_query, en_addref, en_release, en_next, en_skip, en_reset, en_clone };

// The item at `path` (one reference), or NULL when the registry has none.
static item_t *item_new(const wchar_t *path) {
    HKEY k;
    if (rubraview_wlen(path) >= 400 || !open_item(path, &k)) return NULL;
    item_t *it = block_new(sizeof *it);
    if (it) {
        it->cmd.lpVtbl = (IExplorerCommandVtbl *)&cmd_vtbl;
        it->en.lpVtbl = (IEnumExplorerCommandVtbl *)&en_vtbl;
        it->refs = 1;
        (void)rubraview_wcopy_a(it->path, path);
        // Items: the names of the sub keys, in order.
        DWORD type = 0, size = 0;
        if (RegQueryValueExW(k, L"Items", NULL, &type, NULL, &size) == ERROR_SUCCESS && type == REG_MULTI_SZ && size >= 2 * sizeof(wchar_t) &&
            size <= MAX_ITEMS * 64 * sizeof(wchar_t)) {
            wchar_t *list = block_new(size + 2 * sizeof(wchar_t));
            if (list && RegQueryValueExW(k, L"Items", NULL, &type, (BYTE *)list, &size) == ERROR_SUCCESS) {
                for (const wchar_t *s = list; *s && it->nsubs < MAX_ITEMS; s += rubraview_wlen(s) + 1) {
                    if (rubraview_wlen(s) >= 64 || rubraview_wchr(s, L'\\')) continue;
                    (void)rubraview_wcopy_a(it->subs[it->nsubs], s);
                    it->nsubs++;
                }
            }
            block_free(list);
        }
        InterlockedIncrement(&live);
    }
    RegCloseKey(k);
    return it;
}

// ---- the class factory -------------------------------------------------------------------------

typedef struct {
    IClassFactory f;
    LONG          refs;
    wchar_t       clsid[40];
} factory_t;

static HRESULT STDMETHODCALLTYPE f_query(IClassFactory *p, REFIID riid, void **out) {
    if (out == NULL) return E_POINTER;
    *out = NULL;
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_IClassFactory)) return E_NOINTERFACE;
    *out = p;
    InterlockedIncrement(&((factory_t *)p)->refs);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE f_addref(IClassFactory *p) { return (ULONG)InterlockedIncrement(&((factory_t *)p)->refs); }

static ULONG STDMETHODCALLTYPE f_release(IClassFactory *p) {
    LONG n = InterlockedDecrement(&((factory_t *)p)->refs);
    if (n == 0) {
        block_free(p);
        InterlockedDecrement(&live);
    }
    return (ULONG)n;
}

static HRESULT STDMETHODCALLTYPE f_create(IClassFactory *p, IUnknown *outer, REFIID riid, void **out) {
    if (out == NULL) return E_POINTER;
    *out = NULL;
    if (outer) return CLASS_E_NOAGGREGATION;
    item_t *it = item_new(((factory_t *)p)->clsid);
    if (it == NULL) return E_FAIL;
    HRESULT hr = item_query(it, riid, out);
    item_release(it);
    return hr;
}

static HRESULT STDMETHODCALLTYPE f_lock(IClassFactory *p, BOOL lock) {
    (void)p;
    if (lock) InterlockedIncrement(&live);
    else InterlockedDecrement(&live);
    return S_OK;
}

static const IClassFactoryVtbl f_vtbl = { f_query, f_addref, f_release, f_create, f_lock };

// Any class the registry has an item for.
STDAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void **out) {
    if (out == NULL) return E_POINTER;
    *out = NULL;
    wchar_t text[40];
    HKEY k;
    if (StringFromGUID2(clsid, text, 40) == 0 || !open_item(text, &k)) return CLASS_E_CLASSNOTAVAILABLE;
    RegCloseKey(k);
    factory_t *f = block_new(sizeof *f);
    if (f == NULL) return E_OUTOFMEMORY;
    f->f.lpVtbl = (IClassFactoryVtbl *)&f_vtbl;
    f->refs = 1;
    (void)rubraview_wcopy_a(f->clsid, text);
    InterlockedIncrement(&live);
    HRESULT hr = f_query(&f->f, riid, out);
    f_release(&f->f);
    return hr;
}

STDAPI DllCanUnloadNow(void) { return live == 0 ? S_OK : S_FALSE; }

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        self = inst;
        DisableThreadLibraryCalls(inst);
    }
    return TRUE;
}
