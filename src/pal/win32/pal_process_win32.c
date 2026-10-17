#ifdef _WIN32
#include <windows.h>
#include <string.h>
#include "rubraview/pal/pal_process.h"

static u8str_t wide_to_u8(proven_arena_t *arena, const WCHAR *wide) {
    int len = WideCharToMultiByte(CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL);
    if (len <= 1) return (u8str_t){0};
    proven_result_mem_mut_t res = proven_arena_alloc(arena, (proven_size_t)len);
    if (!proven_is_ok(res.err)) return (u8str_t){0};
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, (char*)res.value.ptr, len, NULL, NULL);
    return (u8str_t){ .ptr = (const char*)res.value.ptr, .len = (proven_size_t)(len - 1) };
}

bool rubraview_pal_process_ui_language(char *buf, size_t cap) {
    if (!buf || cap == 0) return false;
    WCHAR name[LOCALE_NAME_MAX_LENGTH];
    if (LCIDToLocaleName(MAKELCID(GetUserDefaultUILanguage(), SORT_DEFAULT), name, LOCALE_NAME_MAX_LENGTH, 0) <= 0) return false;
    int n = WideCharToMultiByte(CP_UTF8, 0, name, -1, buf, (int)cap, NULL, NULL);
    return n > 1;
}

u8str_t rubraview_pal_process_executable(proven_arena_t *arena) {
    if (!arena) return (u8str_t){0};
    WCHAR path[32768];
    DWORD n = GetModuleFileNameW(NULL, path, (DWORD)(sizeof(path) / sizeof(path[0])));
    if (n == 0 || n >= sizeof(path) / sizeof(path[0])) return (u8str_t){0};
    return wide_to_u8(arena, path);
}

static bool start_process(u8str_t command_line, DWORD flags) {
    if (command_line.len == 0) return false;

    int wide_len = MultiByteToWideChar(CP_UTF8, 0, command_line.ptr, (int)command_line.len, NULL, 0);
    if (wide_len <= 0) return false;
    /* CreateProcessW may write into the command line, so it gets a
       writable copy rather than a pointer into the caller's arena. */
    WCHAR *wide = (WCHAR*)HeapAlloc(GetProcessHeap(), 0, ((size_t)wide_len + 1) * sizeof(WCHAR));
    if (!wide) return false;
    MultiByteToWideChar(CP_UTF8, 0, command_line.ptr, (int)command_line.len, wide, wide_len);
    wide[wide_len] = L'\0';

    STARTUPINFOW si = { .cb = sizeof(si) };
    PROCESS_INFORMATION pi = {0};
    /* The handles are closed at once — nothing here waits for it, which
       is the point: the process outlives this viewer. */
    BOOL ok = CreateProcessW(NULL, wide, NULL, NULL, FALSE,
                             flags, NULL, NULL, &si, &pi);
    HeapFree(GetProcessHeap(), 0, wide);
    if (!ok) return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

bool rubraview_pal_process_start_console(u8str_t command_line) {
    return start_process(command_line, CREATE_NEW_CONSOLE);   /* a window of its own to report into */
}

bool rubraview_pal_process_start(u8str_t command_line) {
    return start_process(command_line, 0);
}
#endif /* _WIN32 */

/* GetCurrentPackageFamilyName is in kernel32 from Windows 8; looked up by
   name so nothing more is linked. APPMODEL_ERROR_NO_PACKAGE (15700) when
   the program was not started from a package. */
bool rubraview_pal_process_packaged(char *family, size_t cap) {
    typedef LONG (WINAPI *family_fn)(UINT32 *, PWSTR);
    if (family && cap > 0) family[0] = '\0';
    HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    family_fn get = kernel ? (family_fn)(void *)GetProcAddress(kernel, "GetCurrentPackageFamilyName") : NULL;
    if (!get) return false;
    WCHAR name[256];
    UINT32 length = 256;
    if (get(&length, name) != ERROR_SUCCESS) return false;
    if (family && cap > 0 && WideCharToMultiByte(CP_UTF8, 0, name, -1, family, (int)cap, NULL, NULL) <= 0) family[0] = '\0';
    return true;
}
