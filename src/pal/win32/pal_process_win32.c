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

u8str_t rubraview_pal_process_executable(proven_arena_t *arena) {
    if (!arena) return (u8str_t){0};
    WCHAR path[32768];
    DWORD n = GetModuleFileNameW(NULL, path, (DWORD)(sizeof(path) / sizeof(path[0])));
    if (n == 0 || n >= sizeof(path) / sizeof(path[0])) return (u8str_t){0};
    return wide_to_u8(arena, path);
}

bool rubraview_pal_process_start_console(u8str_t command_line) {
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
    /* CREATE_NEW_CONSOLE: a window of its own to report into. The handles
       are closed at once — nothing here waits for it, which is the point:
       the run outlives this viewer. */
    BOOL ok = CreateProcessW(NULL, wide, NULL, NULL, FALSE,
                             CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi);
    HeapFree(GetProcessHeap(), 0, wide);
    if (!ok) return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}
#endif /* _WIN32 */
