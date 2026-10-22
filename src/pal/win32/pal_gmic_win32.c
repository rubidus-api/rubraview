#ifdef _WIN32
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "rubraview/pal/pal_gmic.h"

static bool wide_of(proven_u8str_view_t s, WCHAR *out, int cap) {
    int n = s.size ? MultiByteToWideChar(CP_UTF8, 0, (const char *)s.ptr, (int)s.size, out, cap - 1) : 0;
    if (n <= 0 && s.size) return false;
    out[n] = L'\0';
    return true;
}

static proven_u8str_view_t utf8_of(proven_arena_t *arena, const WCHAR *wide) {
    int len = WideCharToMultiByte(CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL);
    if (len <= 1) return PROVEN_LIT("");
    proven_result_mem_mut_t res = proven_arena_alloc(arena, (proven_size_t)len);
    if (!proven_is_ok(res.err)) return PROVEN_LIT("");
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, (char *)res.value.ptr, len, NULL, NULL);
    return proven_u8str_view_from_bytes(res.value.ptr, (proven_size_t)(len - 1));
}

static bool is_file(const WCHAR *path) {
    DWORD a = GetFileAttributesW(path);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

proven_u8str_view_t rubraview_pal_gmic_find(proven_arena_t *arena, proven_u8str_view_t where) {
    WCHAR path[MAX_PATH + 16];
    if (!arena || where.size == 0 || !wide_of(where, path, MAX_PATH)) return PROVEN_LIT("");
    size_t n = wcslen(path);
    while (n > 0 && (path[n - 1] == L'\\' || path[n - 1] == L'/')) path[--n] = L'\0';
    if (n > 4 && _wcsicmp(path + n - 4, L".exe") == 0) return is_file(path) ? utf8_of(arena, path) : PROVEN_LIT("");
    wcscat(path, L"\\gmic.exe");
    return is_file(path) ? utf8_of(arena, path) : PROVEN_LIT("");
}

proven_u8str_view_t rubraview_pal_gmic_temp(proven_arena_t *arena, uint32_t tag, bool output) {
    WCHAR dir[MAX_PATH + 1], shortened[MAX_PATH + 1], name[MAX_PATH + 64];
    DWORD n = GetTempPathW(MAX_PATH, dir);
    if (!arena || n == 0 || n >= MAX_PATH) return PROVEN_LIT("");
    bool ascii = true;
    for (DWORD i = 0; i < n; ++i) ascii = ascii && dir[i] < 0x80;
    /* gmic.exe reads its arguments in the system's code page: a name outside ASCII is passed as its short form */
    if (!ascii) {
        DWORD s = GetShortPathNameW(dir, shortened, MAX_PATH);
        if (s == 0 || s >= MAX_PATH) return PROVEN_LIT("");
        for (DWORD i = 0; i < s; ++i) if (shortened[i] >= 0x80) return PROVEN_LIT("");
        wcscpy(dir, shortened);
    }
    _snwprintf(name, sizeof(name) / sizeof(name[0]), L"%lsrubraview-gmic-%lu-%lu-%ls.png", dir,
               (unsigned long)GetCurrentProcessId(), (unsigned long)tag, output ? L"out" : L"in");
    name[sizeof(name) / sizeof(name[0]) - 1] = L'\0';
    return utf8_of(arena, name);
}

/* One argument as CommandLineToArgvW reads it back: quoted when it holds a space, a tab or a quote (or is
   empty), with the backslashes before a quote doubled. */
static bool append_arg(WCHAR *line, size_t cap, size_t *at, const WCHAR *arg) {
    bool plain = arg[0] != L'\0' && !wcspbrk(arg, L" \t\"");
    size_t n = *at;
#define PUT(c) do { if (n + 1 >= cap) return false; line[n++] = (c); } while (0)
    if (n > 0) PUT(L' ');
    if (plain) {
        for (const WCHAR *p = arg; *p; ++p) PUT(*p);
    } else {
        PUT(L'"');
        for (const WCHAR *p = arg;; ++p) {
            size_t slashes = 0;
            while (*p == L'\\') { slashes++; p++; }
            if (*p == L'\0') { for (size_t i = 0; i < slashes * 2; ++i) PUT(L'\\'); break; }
            if (*p == L'"') { for (size_t i = 0; i < slashes * 2 + 1; ++i) PUT(L'\\'); PUT(L'"'); continue; }
            for (size_t i = 0; i < slashes; ++i) PUT(L'\\');
            PUT(*p);
        }
        PUT(L'"');
    }
#undef PUT
    line[n] = L'\0';
    *at = n;
    return true;
}

/* What it writes, its last bytes kept: what an error says is at the end. */
static void keep_tail(rubraview_gmic_exec_t *r, size_t *used, const char *data, DWORD n) {
    size_t cap = sizeof(r->said) - 1;
    for (DWORD i = 0; i < n; ++i) {
        char c = data[i];
        if (c == '\r') continue;
        if ((unsigned char)c < 0x20 && c != '\n' && c != '\t') continue;   /* colour codes and the like */
        if (*used == cap) { memmove(r->said, r->said + 1, cap - 1); (*used)--; }
        r->said[(*used)++] = c;
    }
    r->said[*used] = '\0';
}

static void drain(HANDLE pipe, rubraview_gmic_exec_t *r, size_t *used) {
    char buf[4096];
    DWORD avail = 0, got = 0;
    while (PeekNamedPipe(pipe, NULL, 0, NULL, &avail, NULL) && avail > 0 &&
           ReadFile(pipe, buf, avail < sizeof(buf) ? avail : (DWORD)sizeof(buf), &got, NULL) && got > 0) {
        keep_tail(r, used, buf, got);
    }
}

rubraview_gmic_exec_t rubraview_pal_gmic_exec(proven_u8str_view_t exe, const proven_u8str_view_t *items, size_t count,
                                              uint32_t timeout_ms, bool (*tick)(void *context), void *context) {
    rubraview_gmic_exec_t r = { .exit_code = -1 };
    static WCHAR line[32768];   /* Windows' longest command line; one run at a time (callers hold the edit thread) */
    static SRWLOCK line_lock = SRWLOCK_INIT;
    WCHAR program[MAX_PATH + 1], arg[8192];
    if (exe.size == 0 || count > RUBRAVIEW_GMIC_MAX_ITEMS || !wide_of(exe, program, MAX_PATH)) {
        snprintf(r.said, sizeof(r.said), "%s", "gmic.exe was not found");
        return r;
    }
    AcquireSRWLockExclusive(&line_lock);
    size_t at = 0;
    bool fits = append_arg(line, sizeof(line) / sizeof(line[0]), &at, program);
    for (size_t i = 0; i < count && fits; ++i) {
        fits = wide_of(items[i], arg, (int)(sizeof(arg) / sizeof(arg[0]))) && append_arg(line, sizeof(line) / sizeof(line[0]), &at, arg);
    }
    if (!fits) {
        ReleaseSRWLockExclusive(&line_lock);
        snprintf(r.said, sizeof(r.said), "%s", "the command is too long");
        return r;
    }

    SECURITY_ATTRIBUTES sa = { .nLength = sizeof(sa), .bInheritHandle = TRUE };
    HANDLE read_end = NULL, write_end = NULL;
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);
    if (!CreatePipe(&read_end, &write_end, &sa, 0) || nul == INVALID_HANDLE_VALUE) {
        if (read_end) CloseHandle(read_end);
        if (write_end) CloseHandle(write_end);
        if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
        ReleaseSRWLockExclusive(&line_lock);
        snprintf(r.said, sizeof(r.said), "%s", "gmic.exe could not be started");
        return r;
    }
    SetHandleInformation(read_end, HANDLE_FLAG_INHERIT, 0);

    SIZE_T attr_size = 0;
    InitializeProcThreadAttributeList(NULL, 1, 0, &attr_size);
    LPPROC_THREAD_ATTRIBUTE_LIST attrs = (LPPROC_THREAD_ATTRIBUTE_LIST)malloc(attr_size);
    HANDLE inherit[2] = { write_end, nul };   /* its output and an empty input, nothing else of ours */
    bool attrs_ok = attrs && InitializeProcThreadAttributeList(attrs, 1, 0, &attr_size) &&
                    UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit, sizeof(inherit), NULL, NULL);
    STARTUPINFOEXW si = { .StartupInfo = { .cb = sizeof(STARTUPINFOEXW), .dwFlags = STARTF_USESTDHANDLES,
                                           .hStdInput = nul, .hStdOutput = write_end, .hStdError = write_end },
                          .lpAttributeList = attrs_ok ? attrs : NULL };
    /* Its own folder is where it runs: its DLLs and nothing of the viewer's folder */
    WCHAR folder[MAX_PATH + 1];
    wcscpy(folder, program);
    WCHAR *slash = wcsrchr(folder, L'\\');
    if (slash) *slash = L'\0';
    PROCESS_INFORMATION pi = {0};
    HANDLE job = CreateJobObjectW(NULL, NULL);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {0};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION;
    if (job) SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
    BOOL started = attrs_ok && CreateProcessW(program, line, NULL, NULL, TRUE,
                                              CREATE_SUSPENDED | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT,
                                              NULL, slash ? folder : NULL, &si.StartupInfo, &pi);
    ReleaseSRWLockExclusive(&line_lock);
    if (attrs_ok) DeleteProcThreadAttributeList(attrs);
    free(attrs);
    CloseHandle(write_end);   /* only the child writes now: its end closes when it ends */
    CloseHandle(nul);
    if (!started) {
        CloseHandle(read_end);
        if (job) CloseHandle(job);
        snprintf(r.said, sizeof(r.said), "%s", "gmic.exe could not be started");
        return r;
    }
    if (job) AssignProcessToJobObject(job, pi.hProcess);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);

    size_t used = 0;
    ULONGLONG begun = GetTickCount64();
    for (;;) {
        bool ended = WaitForSingleObject(pi.hProcess, 50) == WAIT_OBJECT_0;
        drain(read_end, &r, &used);
        if (ended) break;
        bool late = timeout_ms && GetTickCount64() - begun > timeout_ms;
        bool stop = tick && tick(context);
        if (late || stop) {
            if (job) TerminateJobObject(job, 4); else TerminateProcess(pi.hProcess, 4);
            WaitForSingleObject(pi.hProcess, 2000);
            r.cut = true;
            r.cancelled = stop;
            break;
        }
    }
    drain(read_end, &r, &used);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    r.exit_code = (int)code;
    r.ok = !r.cut && code == 0;
    CloseHandle(pi.hProcess);
    CloseHandle(read_end);
    if (job) CloseHandle(job);
    return r;
}
#endif
