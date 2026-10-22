#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rubraview/pal/pal_plugin.h"
#include "rubraview/plugin_filter.h"
#include "rubraview/lang.h"

/*
 * RFC-0007 (RV-138, D-186): a plug-in is somebody else's code, so it runs in a child process: this same
 * program started with `--filter-host <mapping> <dll>`, sharing one file mapping (inherited by its handle,
 * and only it) that holds rubraview_plugin_shared_t and the two pictures. A job object ends the child and
 * anything it started; a crash ends the child only.
 */

#define DESCRIBE_MS 10000u   /* a plug-in that does not say what it is in this long is refused */
#define CANCEL_GRACE_MS 5000u

static _Thread_local char g_why[300];

static void wide_of(proven_u8str_view_t s, WCHAR *out, size_t cap) {
    int n = s.size ? MultiByteToWideChar(CP_UTF8, 0, (const char *)s.ptr, (int)s.size, out, (int)cap - 1) : 0;
    out[n > 0 ? n : 0] = L'\0';
}

static void utf8_of(const WCHAR *w, char *out, size_t cap) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, out, (int)cap, NULL, NULL);
    if (n <= 0) out[0] = '\0';
}

bool rubraview_pal_plugins_default_folder(char *out, size_t capacity, bool make) {
    if (!out || capacity == 0) return false;
    out[0] = '\0';
    PWSTR local = NULL;
    if (FAILED(SHGetKnownFolderPath(&FOLDERID_LocalAppData, 0, NULL, &local)) || !local) return false;
    WCHAR path[MAX_PATH];
    int n = _snwprintf(path, MAX_PATH, L"%ls\\Rubraview\\plugins", local);
    CoTaskMemFree(local);
    if (n <= 0 || n >= MAX_PATH) return false;
    if (make) {
        WCHAR parent[MAX_PATH];
        wcscpy(parent, path);
        WCHAR *cut = wcsrchr(parent, L'\\');
        if (cut) { *cut = L'\0'; CreateDirectoryW(parent, NULL); }
        CreateDirectoryW(path, NULL);
    }
    utf8_of(path, out, capacity);
    return out[0] != '\0';
}

/* The machine a DLL is built for, from its PE header; 0 when it is not one. */
static WORD pe_machine(const uint8_t *b, size_t n) {
    if (n < 0x40 || b[0] != 'M' || b[1] != 'Z') return 0;
    uint32_t pe = (uint32_t)b[0x3C] | (uint32_t)b[0x3D] << 8 | (uint32_t)b[0x3E] << 16 | (uint32_t)b[0x3F] << 24;
    if ((size_t)pe + 6 > n || memcmp(b + pe, "PE\0\0", 4) != 0) return 0;
    return (WORD)(b[pe + 4] | b[pe + 5] << 8);
}

static bool read_whole(const WCHAR *path, uint8_t **data, size_t *size) {
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER len;
    bool ok = GetFileSizeEx(f, &len) && len.QuadPart > 0 && len.QuadPart < (1LL << 30);
    uint8_t *buf = ok ? (uint8_t*)malloc((size_t)len.QuadPart) : NULL;
    DWORD got = 0;
    ok = buf && ReadFile(f, buf, (DWORD)len.QuadPart, &got, NULL) && got == (DWORD)len.QuadPart;
    CloseHandle(f);
    if (!ok) { free(buf); return false; }
    *data = buf;
    *size = (size_t)len.QuadPart;
    return true;
}

/* The child started on `mapping`, waited for: true when it ended by itself with the block DONE. */
static bool child_run(HANDLE mapping, rubraview_plugin_shared_t *shared, const char *dll_utf8, DWORD limit_ms,
                      bool (*tick)(void *context, double progress), void *context, const char **why) {
    WCHAR exe[MAX_PATH], dll[MAX_PATH], line[2 * MAX_PATH + 64];
    if (!GetModuleFileNameW(NULL, exe, MAX_PATH)) { *why = N_("the plug-in could not be started"); return false; }
    wide_of((proven_u8str_view_t){ .ptr = (const proven_byte_t *)dll_utf8, .size = strlen(dll_utf8) }, dll, MAX_PATH);
    _snwprintf(line, sizeof(line) / sizeof(line[0]), L"\"%ls\" --filter-host %llu \"%ls\"", exe,
               (unsigned long long)(uintptr_t)mapping, dll);

    SIZE_T attr_size = 0;
    InitializeProcThreadAttributeList(NULL, 1, 0, &attr_size);
    LPPROC_THREAD_ATTRIBUTE_LIST attrs = (LPPROC_THREAD_ATTRIBUTE_LIST)malloc(attr_size);
    if (!attrs || !InitializeProcThreadAttributeList(attrs, 1, 0, &attr_size)) { free(attrs); *why = N_("the plug-in could not be started"); return false; }
    HANDLE inherit[1] = { mapping };   /* the mapping and nothing else of ours */
    UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit, sizeof(inherit), NULL, NULL);
    STARTUPINFOEXW si = { .StartupInfo = { .cb = sizeof(STARTUPINFOEXW) }, .lpAttributeList = attrs };
    PROCESS_INFORMATION pi = {0};
    HANDLE job = CreateJobObjectW(NULL, NULL);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {0};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION;
    if (job) SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
    BOOL started = CreateProcessW(exe, line, NULL, NULL, TRUE, CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW,
                                  NULL, NULL, &si.StartupInfo, &pi);
    DeleteProcThreadAttributeList(attrs);
    free(attrs);
    if (!started) { if (job) CloseHandle(job); *why = N_("the plug-in could not be started"); return false; }
    if (job) AssignProcessToJobObject(job, pi.hProcess);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);

    ULONGLONG begun = GetTickCount64(), cancelled_at = 0;
    bool ended = false, cut = false;
    while (!ended) {
        ended = WaitForSingleObject(pi.hProcess, 50) == WAIT_OBJECT_0;
        if (ended) break;
        ULONGLONG now = GetTickCount64();
        if (tick && !cancelled_at && tick(context, shared->progress)) {
            shared->cancel = 1;
            cancelled_at = now;
        }
        if ((limit_ms && now - begun > limit_ms) || (cancelled_at && now - cancelled_at > CANCEL_GRACE_MS)) {
            if (job) TerminateJobObject(job, 4); else TerminateProcess(pi.hProcess, 4);
            WaitForSingleObject(pi.hProcess, 2000);
            cut = true;
            break;
        }
    }
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    if (job) CloseHandle(job);
    if (cut) { *why = cancelled_at ? N_("the plug-in did not stop when asked; it was ended") : N_("the plug-in did not answer in time; it was ended"); return false; }
    if (shared->state == RUBRAVIEW_PLUGIN_DONE && code == 0) return true;
    if (cancelled_at) { *why = N_("cancelled"); return false; }
    if (shared->state == RUBRAVIEW_PLUGIN_FAILED && shared->error[0]) {
        snprintf(g_why, sizeof(g_why), "%.*s", (int)sizeof(shared->error), shared->error);   /* the child's words: never past their field (D-189) */
        *why = g_why;
        return false;
    }
    *why = N_("the plug-in stopped: it crashed");
    return false;
}

/* What was found before, by path and hash: a plug-in that has not changed is not started again to say what it is. */
static rubraview_plugin_t g_known[RUBRAVIEW_PLUGIN_MAX];
static size_t g_known_count;
static SRWLOCK g_known_lock = SRWLOCK_INIT;

static void describe(rubraview_plugin_t *p) {
    for (size_t i = 0; i < g_known_count; ++i) {
        if (strcmp(g_known[i].path, p->path) == 0 && memcmp(g_known[i].sha256, p->sha256, 32) == 0) { *p = g_known[i]; return; }
    }
    size_t bytes = rubraview_plugin_shared_bytes(1, 1);
    SECURITY_ATTRIBUTES sa = { .nLength = sizeof(sa), .bInheritHandle = TRUE };
    HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, &sa, PAGE_READWRITE, 0, (DWORD)bytes, NULL);
    rubraview_plugin_shared_t *s = mapping ? (rubraview_plugin_shared_t*)MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0) : NULL;
    const char *why = N_("the plug-in could not be started");
    if (s) {
        rubraview_plugin_shared_init(s, RUBRAVIEW_PLUGIN_OP_DESCRIBE, 1, 1);
        if (child_run(mapping, s, p->path, DESCRIBE_MS, NULL, NULL, &why)) (void)rubraview_plugin_take_description(s, p, &why);
        UnmapViewOfFile(s);
    }
    if (mapping) CloseHandle(mapping);
    if (!p->usable) p->why = why;
    if (g_known_count < RUBRAVIEW_PLUGIN_MAX) g_known[g_known_count++] = *p;
}

size_t rubraview_pal_plugins_find(proven_u8str_view_t folder, rubraview_plugin_t *out, size_t capacity) {
    if (!out || capacity == 0 || folder.size == 0) return 0;
    WCHAR dir[MAX_PATH], pattern[MAX_PATH];
    wide_of(folder, dir, MAX_PATH);
    size_t dn = wcslen(dir);
    while (dn > 0 && (dir[dn - 1] == L'\\' || dir[dn - 1] == L'/')) dir[--dn] = L'\0';
    if (_snwprintf(pattern, MAX_PATH, L"%ls\\*.rvf.dll", dir) <= 0) return 0;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    size_t count = 0;
    AcquireSRWLockExclusive(&g_known_lock);
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        rubraview_plugin_t *p = &out[count];
        memset(p, 0, sizeof(*p));
        WCHAR full[MAX_PATH];
        if (_snwprintf(full, MAX_PATH, L"%ls\\%ls", dir, fd.cFileName) <= 0) continue;
        utf8_of(fd.cFileName, p->file, sizeof(p->file));
        utf8_of(full, p->path, sizeof(p->path));
        uint8_t *data = NULL;
        size_t size = 0;
        if (!read_whole(full, &data, &size)) { p->why = N_("it could not be read"); count++; continue; }
        rubraview_sha256(data, size, p->sha256);
        WORD machine = pe_machine(data, size);
        free(data);
        if (machine == IMAGE_FILE_MACHINE_I386) p->why = N_("it is a 32-bit DLL; Rubraview takes 64-bit plug-ins");
        else if (machine != IMAGE_FILE_MACHINE_AMD64) p->why = N_("it is not a DLL for this processor");
        else describe(p);
        count++;
    } while (count < capacity && FindNextFileW(h, &fd));
    ReleaseSRWLockExclusive(&g_known_lock);
    FindClose(h);
    return count;
}

bool rubraview_pal_plugin_run(const rubraview_plugin_t *plugin, const double *params, const uint8_t *bgra,
                              int32_t width, int32_t height, uint8_t *out,
                              bool (*tick)(void *context, double progress), void *context, const char **why) {
    const char *dummy;
    if (!why) why = &dummy;
    *why = NULL;
    if (!plugin || !plugin->usable || !bgra || !out) { *why = N_("the plug-in is not there"); return false; }
    size_t bytes = rubraview_plugin_shared_bytes(width, height);
    if (!bytes) { *why = N_("the picture is too large for a plug-in"); return false; }
    SECURITY_ATTRIBUTES sa = { .nLength = sizeof(sa), .bInheritHandle = TRUE };
    HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, &sa, PAGE_READWRITE, (DWORD)((uint64_t)bytes >> 32), (DWORD)bytes, NULL);
    rubraview_plugin_shared_t *s = mapping ? (rubraview_plugin_shared_t*)MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0) : NULL;
    bool ok = false;
    if (s) {
        rubraview_plugin_shared_init(s, RUBRAVIEW_PLUGIN_OP_APPLY, width, height);
        s->param_count = plugin->param_count;
        for (int32_t i = 0; i < plugin->param_count; ++i) s->params[i] = params ? params[i] : plugin->params[i].value;
        size_t picture = (size_t)width * (size_t)height * 4u;
        memcpy(rubraview_plugin_shared_in(s), bgra, picture);
        ok = child_run(mapping, s, plugin->path, 0, tick, context, why);
        if (ok) memcpy(out, rubraview_plugin_shared_out(s), picture);
        UnmapViewOfFile(s);
    } else {
        *why = N_("there is not enough memory for the plug-in's picture");
    }
    if (mapping) CloseHandle(mapping);
    return ok;
}

/* ---- the child ---- */

static rubraview_plugin_shared_t *g_child;

static LONG WINAPI child_crash(EXCEPTION_POINTERS *e) {
    (void)e;
    if (g_child) {
        snprintf(g_child->error, sizeof(g_child->error), "%s", "the plug-in stopped: it crashed");
        g_child->state = RUBRAVIEW_PLUGIN_FAILED;
    }
    TerminateProcess(GetCurrentProcess(), 3);
    return EXCEPTION_EXECUTE_HANDLER;
}

static int child_progress(void *context, double done) {
    rubraview_plugin_shared_t *s = (rubraview_plugin_shared_t*)context;
    s->progress = done < 0.0 ? 0.0 : done > 1.0 ? 1.0 : done;
    return s->cancel != 0;
}

static void child_text(char *dst, size_t cap, const char *src) {
    snprintf(dst, cap, "%s", src ? src : "");
}

int rubraview_pal_plugin_host_main(const wchar_t *mapping_text, const wchar_t *dll) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    SetUnhandledExceptionFilter(child_crash);
    HANDLE mapping = (HANDLE)(uintptr_t)_wcstoui64(mapping_text, NULL, 10);
    rubraview_plugin_shared_t *s = (rubraview_plugin_shared_t*)MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    if (!s || s->magic != RUBRAVIEW_PLUGIN_MAGIC) return 2;
    g_child = s;
    s->state = RUBRAVIEW_PLUGIN_RUNNING;
    HMODULE module = LoadLibraryExW(dll, NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    const rvf_info *(*describe_fn)(void) = module ? (const rvf_info *(*)(void))(void*)GetProcAddress(module, "rvf_describe") : NULL;
    int (*apply_fn)(const rvf_image*, rvf_image*, const double*, rvf_progress, void*) =
        module ? (int (*)(const rvf_image*, rvf_image*, const double*, rvf_progress, void*))(void*)GetProcAddress(module, "rvf_apply") : NULL;
    if (!describe_fn || !apply_fn) {
        child_text(s->error, sizeof(s->error), module ? "it does not export rvf_describe and rvf_apply" : "it could not be loaded");
        s->state = RUBRAVIEW_PLUGIN_FAILED;
        return 1;
    }
    const rvf_info *info = describe_fn();
    if (!info) { child_text(s->error, sizeof(s->error), "it said nothing of itself"); s->state = RUBRAVIEW_PLUGIN_FAILED; return 1; }
    if (s->op == RUBRAVIEW_PLUGIN_OP_DESCRIBE) {
        s->abi = info->abi;
        child_text(s->name, sizeof(s->name), info->name);
        child_text(s->author, sizeof(s->author), info->author);
        child_text(s->version, sizeof(s->version), info->version);
        child_text(s->licence, sizeof(s->licence), info->licence);
        s->described = info->param_count;
        for (int i = 0; i < info->param_count && i < RUBRAVIEW_PLUGIN_PARAMS && info->params; ++i) {
            const rvf_param *p = &info->params[i];
            rubraview_plugin_param_t *q = &s->described_params[i];
            child_text(q->name, sizeof(q->name), p->name);
            child_text(q->choices, sizeof(q->choices), p->choices);
            q->kind = p->kind; q->min = p->min; q->max = p->max; q->step = p->step; q->value = p->value;
        }
        s->state = RUBRAVIEW_PLUGIN_DONE;
        return 0;
    }
    rvf_image in = { rubraview_plugin_shared_in(s), s->width, s->height, s->width * 4 };
    rvf_image out = { rubraview_plugin_shared_out(s), s->width, s->height, s->width * 4 };
    int r = apply_fn(&in, &out, s->params, child_progress, s);
    if (r != 0) {
        child_text(s->error, sizeof(s->error), s->cancel ? "cancelled" : "the plug-in could not do it");
        s->state = RUBRAVIEW_PLUGIN_FAILED;
        return 1;
    }
    s->state = RUBRAVIEW_PLUGIN_DONE;
    return 0;
}

#endif /* _WIN32 */
