/*
 * RV-279: the child that shows Explorer's own menus for the viewer (`rubraview.exe --shell-menu-host`).
 *
 * Owner, 2026-11-14: "공식api쪽은 별개프로세스로 실행하게 해 줘요". Everything here loads other people's code
 * (shell extensions, package COM servers), so it runs in a process of its own: when one of them crashes or hangs, this
 * process goes and the viewer stays. Its window is owned by nobody (an owner across processes would join the two
 * input queues, and a hang here would freeze the viewer); it takes the foreground the viewer allowed it.
 *
 *  - classic: IShellFolder::GetUIObjectOf(IContextMenu) for the files, QueryContextMenu, Windows' own popup menu
 *    (IContextMenu2/3 messages relayed, for Send to and Open with), InvokeCommand.
 *  - win11: the same menu with the items Windows 11's menu takes from packages first (`windows.fileExplorerContextMenus`
 *    in each package's manifest: IExplorerCommand objects, created here and asked their title and state).
 *  - verb:<name>: a verb only Windows can run (DelegateExecute and its kind), through ShellExecuteEx.
 *
 * After a command it stays while it has a window on screen or a piece of work holds its process reference (a
 * Properties sheet, a copy to a ZIP), then ends.
 */
#ifdef _WIN32
#include "rubraview/pal/pal_shellverbs.h"
#include "rubraview/pal/pal_wide.h"
#include "rubraview/shellreq.h"
#include "rubraview/menu_identity.h"

#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shobjidl.h>
#include <objbase.h>
#include <tlhelp32.h>
#include <propsys.h>
#include "rubraview/fileprops.h"
#include "rubraview/pal/pal_fileprops.h"
#include <wincrypt.h>
#include <cryptuiapi.h>

#define WIDE2(x) L##x
#define WIDE(x) WIDE2(x)

#define ID_CLASSIC_FIRST 1
#define ID_CLASSIC_LAST  0x6FFF
#define ID_PACKAGE_FIRST 0x7000
#define PACKAGE_MAX      48
#define FILES_MAX        512

static IContextMenu2 *g_cm2;
static IContextMenu3 *g_cm3;

/* What the child did, step by step, when RUBRAVIEW_SHELLMENU_LOG names a file (the VM's checks): the tick, the step,
   a number (an HRESULT, a count, a command). */
static HANDLE g_log = INVALID_HANDLE_VALUE;

static void hlog(const char *step, long long value) {
    if (g_log == INVALID_HANDLE_VALUE) return;
    char line[200];
    int n = rubraview_fmt(line, sizeof line, "{} {} {:x}\r\n", PROVEN_ARG((uint64_t)GetTickCount64()), PROVEN_ARG(step), PROVEN_ARG((uint64_t)value));
    DWORD wrote = 0;
    if (n > 0) WriteFile(g_log, line, (DWORD)((size_t)n < sizeof line ? (size_t)n : sizeof line - 1), &wrote, NULL);
}

static void hlog_open(void) {
    WCHAR path[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"RUBRAVIEW_SHELLMENU_LOG", path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return;
    g_log = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, 0, NULL);
}

/* ---- the process reference: work started by a command keeps the process (SetProcessReference) ---- */

typedef struct process_ref { IUnknown unk; volatile LONG count; } process_ref_t;

static HRESULT STDMETHODCALLTYPE ref_query(IUnknown *self, REFIID iid, void **out) {
    if (!out) return E_POINTER;
    if (IsEqualIID(iid, &IID_IUnknown)) { *out = self; self->lpVtbl->AddRef(self); return S_OK; }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE ref_add(IUnknown *self) { return (ULONG)InterlockedIncrement(&((process_ref_t *)self)->count); }
static ULONG STDMETHODCALLTYPE ref_release(IUnknown *self) { return (ULONG)InterlockedDecrement(&((process_ref_t *)self)->count); }
static IUnknownVtbl g_ref_vtbl = { ref_query, ref_add, ref_release };
static process_ref_t g_ref = { { &g_ref_vtbl }, 1 };

/* ---- the window ---- */

static LRESULT CALLBACK host_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_INITMENUPOPUP: case WM_DRAWITEM: case WM_MEASUREITEM: case WM_MENUCHAR: {
            if (g_cm3) {
                LRESULT result = 0;
                if (SUCCEEDED(g_cm3->lpVtbl->HandleMenuMsg2(g_cm3, msg, wp, lp, &result))) return result;
            } else if (g_cm2 && msg != WM_MENUCHAR) {
                if (SUCCEEDED(g_cm2->lpVtbl->HandleMenuMsg(g_cm2, msg, wp, lp))) return msg == WM_INITMENUPOPUP ? 0 : TRUE;
            }
            break;
        }
        default: break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static HWND make_window(int32_t x, int32_t y) {
    WNDCLASSW wc = { .lpfnWndProc = host_proc, .hInstance = GetModuleHandleW(NULL), .lpszClassName = L"RubraviewShellMenuHost" };
    RegisterClassW(&wc);
    return CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"Rubraview", WS_POPUP, x, y, 1, 1, NULL, NULL, wc.hInstance, NULL);
}

static void pump(void) {
    MSG m;
    while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }
}

typedef struct seek_windows { HWND host; DWORD pid; bool found; } seek_windows_t;

static BOOL CALLBACK seek_one(HWND hwnd, LPARAM lp) {
    seek_windows_t *s = (seek_windows_t *)lp;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == s->pid && hwnd != s->host && IsWindowVisible(hwnd)) { s->found = true; return FALSE; }
    return TRUE;
}

/* This process's threads now. */
static DWORD thread_count(void) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    DWORD pid = GetCurrentProcessId(), n = 0;
    THREADENTRY32 te = { .dwSize = sizeof te };
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) if (te.th32OwnerProcessID == pid) n++;
    CloseHandle(snap);
    return n;
}

static DWORD g_threads_before;

/* The process reference set before a command runs: what it starts in the background holds it (SetProcessReference,
   and SHSetInstanceExplorer, which older shell code asks through SHGetInstanceExplorer). */
static void hold_process(void) {
    g_threads_before = thread_count();
    SetProcessReference(&g_ref.unk);
    SHSetInstanceExplorer(&g_ref.unk);
}

/* Stay while a command's work goes on: a window of this process on screen, the process reference held, or a thread
   the command started still there (a Properties sheet is made on a thread of its own, its window a moment later;
   VM: the process had ended before it showed). Threads alone keep it 60 s at most: a pool's idle threads go in time. */
static void linger(HWND host) {
    ULONGLONG start = GetTickCount64(), idle_since = 0, threads_since = 0;
    for (;;) {
        MsgWaitForMultipleObjects(0, NULL, FALSE, 250, QS_ALLINPUT);
        pump();
        seek_windows_t s = { host, GetCurrentProcessId(), false };
        EnumWindows(seek_one, (LPARAM)&s);
        ULONGLONG now = GetTickCount64();
        bool held = s.found || g_ref.count > 1;
        bool threads = !held && thread_count() > g_threads_before;
        if (held) threads_since = 0;
        else if (threads && threads_since == 0) threads_since = now;
        bool busy = held || (threads && now - threads_since < 60000);
        if (busy) { idle_since = 0; continue; }
        if (idle_since == 0) idle_since = now;
        if (now - idle_since >= 2000 && now - start >= 4000) break;
    }
    hlog("linger ended", (long long)(GetTickCount64() - start));
}

/* ---- the files ---- */

static size_t read_list(const WCHAR *list_file, WCHAR ***out_paths) {
    *out_paths = NULL;
    HANDLE f = CreateFileW(list_file, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    LARGE_INTEGER size = {0};
    if (!GetFileSizeEx(f, &size) || size.QuadPart <= 0 || size.QuadPart > 8 * 1024 * 1024) { CloseHandle(f); return 0; }
    char *text = (char *)rubraview_alloc((size_t)size.QuadPart + 1);
    DWORD got = 0;
    bool ok = text && ReadFile(f, text, (DWORD)size.QuadPart, &got, NULL) && got == (DWORD)size.QuadPart;
    CloseHandle(f);
    DeleteFileW(list_file);   /* the viewer's, written for this run only */
    if (!ok) { rubraview_free(text); return 0; }
    static proven_u8str_view_t lines[FILES_MAX];
    size_t n = rubraview_shellreq_split_lines(rubraview_bytes_view(text, got), lines, FILES_MAX);
    if (n > FILES_MAX) n = FILES_MAX;
    WCHAR **paths = (WCHAR **)rubraview_alloc_zero(n ? n : 1, sizeof *paths);
    size_t kept = 0;
    for (size_t i = 0; paths && i < n; ++i) {
        int w = MultiByteToWideChar(CP_UTF8, 0, (const char *)lines[i].ptr, (int)lines[i].size, NULL, 0);
        if (w <= 0) continue;
        WCHAR *p = (WCHAR *)rubraview_alloc_zero((size_t)w + 1, sizeof(WCHAR));
        if (!p) continue;
        MultiByteToWideChar(CP_UTF8, 0, (const char *)lines[i].ptr, (int)lines[i].size, p, w);
        paths[kept++] = p;
    }
    rubraview_free(text);
    *out_paths = paths;
    return kept;
}

/* The files as Explorer's items: their absolute ID lists (one folder's: those after the first that are elsewhere
   are left out), the parent folder and the children's last parts. */
typedef struct items {
    PIDLIST_ABSOLUTE abs[FILES_MAX];
    PCUITEMID_CHILD child[FILES_MAX];
    UINT count;
    IShellFolder *parent;
} items_t;

static bool items_make(items_t *it, WCHAR **paths, size_t count) {
    *it = (items_t){0};
    for (size_t i = 0; i < count && it->count < FILES_MAX; ++i) {
        PIDLIST_ABSOLUTE pidl = NULL;
        if (FAILED(SHParseDisplayName(paths[i], NULL, &pidl, 0, NULL)) || !pidl) continue;
        if (it->count > 0) {   /* the same folder as the first */
            PIDLIST_ABSOLUTE a = ILClone(it->abs[0]), b = ILClone(pidl);
            bool same = a && b && ILRemoveLastID((PUIDLIST_RELATIVE)a) && ILRemoveLastID((PUIDLIST_RELATIVE)b) && ILIsEqual(a, b);
            ILFree(a);
            ILFree(b);
            if (!same) { ILFree(pidl); continue; }
        }
        it->abs[it->count++] = pidl;
    }
    if (it->count == 0) return false;
    PCUITEMID_CHILD last = NULL;
    if (FAILED(SHBindToParent(it->abs[0], &IID_IShellFolder, (void **)&it->parent, &last)) || !it->parent) return false;
    for (UINT i = 0; i < it->count; ++i) it->child[i] = ILFindLastID(it->abs[i]);
    return true;
}

static void items_free(items_t *it) {
    if (it->parent) it->parent->lpVtbl->Release(it->parent);
    for (UINT i = 0; i < it->count; ++i) ILFree(it->abs[i]);
    *it = (items_t){0};
}

/* ---- Windows 11's package items ---- */

typedef struct package_cmd { IExplorerCommand *cmd; } package_cmd_t;

static package_cmd_t g_pkg[PACKAGE_MAX];
static UINT g_pkg_count;

static bool attr(const char *tag, const char *tag_end, const char *name, char *out, size_t cap) {
    size_t nlen = proven_cstr_len(name);
    for (const char *p = tag; p + nlen + 2 < tag_end; ++p) {
        if ((p == tag || p[-1] == ' ' || p[-1] == '\t' || p[-1] == '\n' || p[-1] == '\r') &&
            proven_sys_mem_cmp(p, name, nlen) == 0 && p[nlen] == '=' && (p[nlen + 1] == '"' || p[nlen + 1] == '\'')) {
            char q = p[nlen + 1];
            const char *v = p + nlen + 2, *e = v;
            while (e < tag_end && *e != q) ++e;
            size_t n = (size_t)(e - v);
            if (n >= cap) return false;
            proven_sys_mem_copy(out, v, n);
            out[n] = '\0';
            return true;
        }
    }
    return false;
}

static bool type_matches(const char *type, const WCHAR *path, bool folder) {
    if (folder) return rubraview_cstr_cmp_ci(type, "Directory") == 0;
    if (rubraview_cstr_cmp(type, "*") == 0) return true;
    const WCHAR *ext = PathFindExtensionW(path);
    char ext8[64];
    if (!ext || !*ext || WideCharToMultiByte(CP_UTF8, 0, ext, -1, ext8, sizeof ext8, NULL, NULL) <= 0) return false;
    return rubraview_cstr_cmp_ci(type, ext8) == 0;
}

static void add_command(IExplorerCommand *cmd) {
    if (g_pkg_count < PACKAGE_MAX) g_pkg[g_pkg_count++].cmd = cmd;
    else cmd->lpVtbl->Release(cmd);
}

/* A package's command is created on a thread of its own, in the multithreaded apartment, and handed over through the
   global interface table: some take seconds (the VM: four took 11.5 s together, and the menu then lost the
   foreground), so those not made within PACKAGE_WAIT_MS are left out of this menu. Their threads finish on their own. */
#define PACKAGE_WAIT_MS 2500

typedef struct package_job {
    CLSID id;
    DWORD cookie;
    HRESULT hr;
    volatile LONG done;
    ULONGLONG ms;
} package_job_t;

static package_job_t g_jobs[PACKAGE_MAX];
static UINT g_job_count;
static IGlobalInterfaceTable *g_git;

static DWORD WINAPI package_create(void *arg) {
    package_job_t *j = (package_job_t *)arg;
    ULONGLONG t0 = GetTickCount64();
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    IExplorerCommand *cmd = NULL;
    j->hr = CoCreateInstance(&j->id, NULL, CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER, &IID_IExplorerCommand, (void **)&cmd);
    if (SUCCEEDED(j->hr) && cmd) {
        j->hr = g_git->lpVtbl->RegisterInterfaceInGlobal(g_git, (IUnknown *)cmd, &IID_IExplorerCommand, &j->cookie);
        cmd->lpVtbl->Release(cmd);
    }
    j->ms = GetTickCount64() - t0;
    InterlockedExchange(&j->done, 1);
    return 0;   /* the apartment is kept by CoIncrementMTAUsage while the process lives */
}

static void queue_command(const CLSID *id) {
    for (UINT i = 0; i < g_job_count; ++i) if (IsEqualCLSID(&g_jobs[i].id, id)) return;   /* one class, once */
    if (g_job_count < PACKAGE_MAX) g_jobs[g_job_count++].id = *id;
}

static void run_jobs(void) {
    if (g_job_count == 0) return;
    CO_MTA_USAGE_COOKIE mta = NULL;
    if (FAILED(CoIncrementMTAUsage(&mta))) return;
    if (FAILED(CoCreateInstance(&CLSID_StdGlobalInterfaceTable, NULL, CLSCTX_INPROC_SERVER, &IID_IGlobalInterfaceTable, (void **)&g_git)) || !g_git) return;
    for (UINT i = 0; i < g_job_count; ++i) {
        HANDLE t = CreateThread(NULL, 0, package_create, &g_jobs[i], 0, NULL);
        if (t) { CloseHandle(t); continue; }
        g_jobs[i].hr = E_FAIL;
        InterlockedExchange(&g_jobs[i].done, 1);
    }
    ULONGLONG until = GetTickCount64() + PACKAGE_WAIT_MS;
    for (;;) {
        bool all = true;
        for (UINT i = 0; i < g_job_count; ++i) all = all && g_jobs[i].done;
        if (all || GetTickCount64() >= until) break;
        MsgWaitForMultipleObjects(0, NULL, FALSE, 20, QS_ALLINPUT);
        pump();
    }
    for (UINT i = 0; i < g_job_count; ++i) {
        package_job_t *j = &g_jobs[i];
        if (!j->done) { hlog("package too slow", i); continue; }
        hlog("package ms", (long long)j->ms);
        if (FAILED(j->hr)) { hlog("package failed", j->hr); continue; }
        IExplorerCommand *cmd = NULL;
        if (SUCCEEDED(g_git->lpVtbl->GetInterfaceFromGlobal(g_git, j->cookie, &IID_IExplorerCommand, (void **)&cmd)) && cmd) add_command(cmd);
    }
}

/* One manifest's context menu verbs for this file: their COM objects created. */
static void manifest_commands(const char *xml, const WCHAR *path, bool folder) {
    const char *menus = rubraview_cstr_str(xml, "FileExplorerContextMenus");
    while (menus) {
        const char *menus_end = rubraview_cstr_str(menus + 1, "FileExplorerContextMenus");   /* the closing tag */
        if (!menus_end) break;
        for (const char *it = rubraview_cstr_str(menus, ":ItemType"); it && it < menus_end; it = rubraview_cstr_str(it + 1, ":ItemType")) {
            const char *tag_end = rubraview_cstr_chr(it, '>');
            if (!tag_end || tag_end > menus_end || it[-1] == '/') continue;   /* "</x:ItemType>" */
            char type[128];
            if (!attr(it, tag_end, "Type", type, sizeof type) || !type_matches(type, path, folder)) continue;
            const char *close = rubraview_cstr_str(tag_end, "ItemType>");
            if (!close || close > menus_end) close = menus_end;
            for (const char *v = rubraview_cstr_str(tag_end, ":Verb"); v && v < close; v = rubraview_cstr_str(v + 1, ":Verb")) {
                const char *v_end = rubraview_cstr_chr(v, '>');
                char clsid[64];
                if (!v_end || !attr(v, v_end, "Clsid", clsid, sizeof clsid)) continue;
                if (rubraview_cstr_str(clsid, RUBRAVIEW_MENU_CLSID) || rubraview_cstr_str(clsid, "dea03208-26b9-42f8-a239-14aafb1527dc"))
                    continue;   /* Rubraview's own: its items are in the viewer's list already */
                WCHAR wclsid[64];
                char braced[72];
                if (rubraview_fmt(braced, sizeof braced, clsid[0] == '{' ? "{}" : "{{{}}}", PROVEN_ARG(clsid)) < 0) continue;
                MultiByteToWideChar(CP_UTF8, 0, braced, -1, wclsid, 64);
                CLSID id;
                if (FAILED(CLSIDFromString(wclsid, &id))) continue;
                queue_command(&id);
            }
        }
        menus = rubraview_cstr_str(menus_end + 1, "FileExplorerContextMenus");
    }
}

static char *read_small_file(const WCHAR *path) {
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return NULL;
    LARGE_INTEGER size = {0};
    char *text = NULL;
    if (GetFileSizeEx(f, &size) && size.QuadPart > 0 && size.QuadPart < 4 * 1024 * 1024) {
        text = (char *)rubraview_alloc((size_t)size.QuadPart + 1);
        DWORD got = 0;
        if (text && ReadFile(f, text, (DWORD)size.QuadPart, &got, NULL)) text[got] = '\0';
        else { rubraview_free(text); text = NULL; }
    }
    CloseHandle(f);
    return text;
}

/* Every package of this user: its manifest, where the repository says it is. */
#define PACKAGES_KEY L"Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\CurrentVersion\\AppModel\\Repository\\Packages"

static void package_commands(const WCHAR *path, bool folder) {
    HKEY root = NULL;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, PACKAGES_KEY, 0, KEY_READ, &root) != ERROR_SUCCESS) return;
    WCHAR name[300];
    for (DWORD i = 0;; ++i) {
        DWORD len = 300;
        if (RegEnumKeyExW(root, i, name, &len, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) break;
        WCHAR folder_path[MAX_PATH * 2];
        DWORD size = sizeof folder_path - sizeof(WCHAR), type = 0;
        if (RegGetValueW(root, name, L"PackageRootFolder", RRF_RT_REG_SZ, &type, folder_path, &size) != ERROR_SUCCESS) continue;
        WCHAR manifest[MAX_PATH * 2 + 32];
        if (!rubraview_wcopy(manifest, MAX_PATH * 2 + 32, folder_path) || !rubraview_wcat(manifest, MAX_PATH * 2 + 32, L"\\AppxManifest.xml")) continue;
        char *xml = read_small_file(manifest);
        if (!xml) continue;
        if (rubraview_cstr_str(xml, "FileExplorerContextMenus")) manifest_commands(xml, path, folder);
        rubraview_free(xml);
    }
    RegCloseKey(root);
    run_jobs();
}

/* A package's command into the menu; its subcommands as a submenu, one level down. */
static UINT g_ids_used;
static IExplorerCommand *g_id_cmd[PACKAGE_MAX * 4];

/* At `position` in `menu` (-1: at its end). */
static bool menu_add_command(HMENU menu, IExplorerCommand *cmd, IShellItemArray *arr, bool deeper, int position) {
    EXPCMDSTATE state = ECS_ENABLED;
    HRESULT st = cmd->lpVtbl->GetState(cmd, arr, FALSE, &state);   /* not slow: E_PENDING is shown enabled, as Explorer first draws it */
    if (st == E_PENDING) state = ECS_ENABLED;
    else if (FAILED(st) || (state & ECS_HIDDEN)) return false;
    EXPCMDFLAGS flags = 0;
    cmd->lpVtbl->GetFlags(cmd, &flags);
    UINT where = position < 0 ? (UINT)-1 : (UINT)position;
    if (flags & ECF_ISSEPARATOR) return InsertMenuW(menu, where, MF_BYPOSITION | MF_SEPARATOR, 0, NULL) != 0;
    LPWSTR title = NULL;
    if (FAILED(cmd->lpVtbl->GetTitle(cmd, arr, &title)) || !title) return false;
    bool added = false;
    if ((flags & ECF_HASSUBCOMMANDS) && deeper) {
        IEnumExplorerCommand *e = NULL;
        HMENU sub = CreatePopupMenu();
        if (sub && SUCCEEDED(cmd->lpVtbl->EnumSubCommands(cmd, &e)) && e) {
            IExplorerCommand *child = NULL;
            ULONG got = 0;
            while (e->lpVtbl->Next(e, 1, &child, &got) == S_OK && got == 1 && child) {
                (void)menu_add_command(sub, child, arr, false, -1);   /* kept there by its own reference */
                child->lpVtbl->Release(child);
                child = NULL;
            }
            e->lpVtbl->Release(e);
        }
        added = sub && GetMenuItemCount(sub) > 0 && InsertMenuW(menu, where, MF_BYPOSITION | MF_POPUP | MF_STRING, (UINT_PTR)sub, title);
        if (!added && sub) DestroyMenu(sub);
    } else if (g_ids_used < PACKAGE_MAX * 4) {
        UINT id = ID_PACKAGE_FIRST + g_ids_used;
        added = InsertMenuW(menu, where, MF_BYPOSITION | MF_STRING | ((state & ECS_DISABLED) ? MF_GRAYED : 0), id, title) != 0;
        if (added) { g_id_cmd[g_ids_used++] = cmd; cmd->lpVtbl->AddRef(cmd); }
    }
    CoTaskMemFree(title);
    return added;
}

/* ---- the menus ---- */

static int show_menu(const WCHAR *kind, WCHAR **paths, size_t count, int32_t x, int32_t y) {
    bool win11 = rubraview_wcmp(kind, L"win11") == 0, shift = rubraview_wcmp(kind, L"classic-shift") == 0;
    items_t it;
    if (!items_make(&it, paths, count)) { hlog("items failed", 0); return 3; }
    hlog("items", it.count);
    HWND hwnd = make_window(x, y);
    if (!hwnd) { items_free(&it); return 4; }
    hlog("foreground at once", SetForegroundWindow(hwnd));   /* while the viewer's leave holds: building may take a while */
    IContextMenu *cm = NULL;
    HRESULT ui = it.parent->lpVtbl->GetUIObjectOf(it.parent, hwnd, it.count, it.child, &IID_IContextMenu, NULL, (void **)&cm);
    hlog("GetUIObjectOf", ui);
    if (FAILED(ui) || !cm) {
        DestroyWindow(hwnd);
        items_free(&it);
        return 5;
    }
    cm->lpVtbl->QueryInterface(cm, &IID_IContextMenu3, (void **)&g_cm3);
    if (!g_cm3) cm->lpVtbl->QueryInterface(cm, &IID_IContextMenu2, (void **)&g_cm2);
    HMENU menu = CreatePopupMenu();
    IShellItemArray *arr = NULL;
    /* The classic items first; Windows 11's package items then go in above them. Inserted after, they ended up among
       the classic ones (VM): the shell's own menu places its items where it likes, not at the index it is given. */
    UINT flags = CMF_NORMAL | (shift ? CMF_EXTENDEDVERBS : 0);
    HRESULT q = cm->lpVtbl->QueryContextMenu(cm, menu, 0, ID_CLASSIC_FIRST, ID_CLASSIC_LAST, flags);
    hlog("QueryContextMenu", q);
    if (win11) {
        DWORD attrs = GetFileAttributesW(paths[0]);
        bool folder = attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY);
        if (SUCCEEDED(SHCreateShellItemArrayFromIDLists(it.count, (PCIDLIST_ABSOLUTE_ARRAY)it.abs, &arr)) && arr) {
            package_commands(paths[0], folder);
            hlog("package commands", g_pkg_count);
            int at = 0;
            for (UINT i = 0; i < g_pkg_count; ++i) if (menu_add_command(menu, g_pkg[i].cmd, arr, true, at)) at++;
            if (at > 0 && GetMenuItemCount(menu) > at) InsertMenuW(menu, (UINT)at, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);
        }
    }
    hlog("menu items", GetMenuItemCount(menu));
    int code = 0;
    if (FAILED(q) && GetMenuItemCount(menu) == 0) code = 6;
    if (code == 0) {
        hlog("foreground", SetForegroundWindow(hwnd));
        UINT chosen = (UINT)TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, x, y, hwnd, NULL);
        hlog("chosen", chosen);
        if (chosen == 0) hlog("last error", GetLastError());
        PostMessageW(hwnd, WM_NULL, 0, 0);
        pump();
        if (chosen >= ID_CLASSIC_FIRST && chosen <= ID_CLASSIC_LAST) {
            hold_process();
            CMINVOKECOMMANDINFOEX ci = { .cbSize = sizeof ci };
            ci.fMask = CMIC_MASK_UNICODE | CMIC_MASK_PTINVOKE | (GetKeyState(VK_SHIFT) < 0 ? CMIC_MASK_SHIFT_DOWN : 0) |
                       (GetKeyState(VK_CONTROL) < 0 ? CMIC_MASK_CONTROL_DOWN : 0);
            ci.hwnd = hwnd;
            ci.lpVerb = MAKEINTRESOURCEA(chosen - ID_CLASSIC_FIRST);
            ci.lpVerbW = MAKEINTRESOURCEW(chosen - ID_CLASSIC_FIRST);
            ci.nShow = SW_SHOWNORMAL;
            ci.ptInvoke = (POINT){ x, y };
            HRESULT inv = cm->lpVtbl->InvokeCommand(cm, (LPCMINVOKECOMMANDINFO)&ci);
            hlog("InvokeCommand", inv);
            if (FAILED(inv)) code = 7;
            linger(hwnd);
        } else if (chosen >= ID_PACKAGE_FIRST && chosen < ID_PACKAGE_FIRST + g_ids_used && arr) {
            hold_process();
            IExplorerCommand *cmd = g_id_cmd[chosen - ID_PACKAGE_FIRST];
            HRESULT inv = cmd->lpVtbl->Invoke(cmd, arr, NULL);
            hlog("Invoke", inv);
            if (FAILED(inv)) code = 8;
            linger(hwnd);
        }
    }
    for (UINT i = 0; i < g_ids_used; ++i) g_id_cmd[i]->lpVtbl->Release(g_id_cmd[i]);
    for (UINT i = 0; i < g_pkg_count; ++i) g_pkg[i].cmd->lpVtbl->Release(g_pkg[i].cmd);
    if (arr) arr->lpVtbl->Release(arr);
    if (g_cm3) { g_cm3->lpVtbl->Release(g_cm3); g_cm3 = NULL; }
    if (g_cm2) { g_cm2->lpVtbl->Release(g_cm2); g_cm2 = NULL; }
    cm->lpVtbl->Release(cm);
    DestroyMenu(menu);
    DestroyWindow(hwnd);
    items_free(&it);
    return code;
}

/* A verb Windows runs, by its name, on each file. */
static int run_verb(const WCHAR *verb, WCHAR **paths, size_t count, int32_t x, int32_t y) {
    HWND hwnd = make_window(x, y);
    hold_process();
    int code = 0;
    for (size_t i = 0; i < count; ++i) {
        SHELLEXECUTEINFOW ei = { .cbSize = sizeof ei };
        ei.fMask = SEE_MASK_INVOKEIDLIST | SEE_MASK_FLAG_LOG_USAGE;
        ei.hwnd = hwnd;
        ei.lpVerb = verb;
        ei.lpFile = paths[i];
        ei.nShow = SW_SHOWNORMAL;
        if (!ShellExecuteExW(&ei)) { code = 9; hlog("ShellExecuteEx failed", GetLastError()); }
        else hlog("ShellExecuteEx", i);
    }
    linger(hwnd);
    if (hwnd) DestroyWindow(hwnd);
    return code;
}

/* ---- RV-281: the properties window's work ---- */

/* An answer for the viewer: written beside the list file, under its own name only when whole. */
typedef struct answer { char *buf; size_t len, cap; } answer_t;

static void answer_add(answer_t *a, const char *text, size_t n) {
    if (a->len + n + 1 > a->cap) {
        size_t cap = (a->cap ? a->cap * 2 : 8192);
        while (cap < a->len + n + 1) cap *= 2;
        char *more = (char *)rubraview_realloc(rubraview_mem_mut(a->buf, cap));
        if (!more) return;
        a->buf = more;
        a->cap = cap;
    }
    proven_sys_mem_copy(a->buf + a->len, text, n);
    a->len += n;
    a->buf[a->len] = '\0';
}

static void answer_wide(answer_t *a, const WCHAR *text) {   /* escaped as the lines carry it */
    char u8[2048], esc[4200];
    if (WideCharToMultiByte(CP_UTF8, 0, text ? text : L"", -1, u8, sizeof u8, NULL, NULL) <= 0) u8[0] = '\0';
    if (rubraview_props_escape(proven_u8str_view_from_cstr(u8), esc, sizeof esc)) answer_add(a, esc, proven_cstr_len(esc));
}

static void answer_put(const WCHAR *list_file, const answer_t *a) {
    WCHAR tmp[MAX_PATH * 2 + 8], out[MAX_PATH * 2 + 8];
    if (!rubraview_wcopy(tmp, MAX_PATH * 2 + 8, list_file) || !rubraview_wcat(tmp, MAX_PATH * 2 + 8, L".tmp")) return;
    if (!rubraview_wcopy(out, MAX_PATH * 2 + 8, list_file) || !rubraview_wcat(out, MAX_PATH * 2 + 8, L".out")) return;
    HANDLE f = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD wrote = 0;
    if (a->len) WriteFile(f, a->buf, (DWORD)a->len, &wrote, NULL);
    CloseHandle(f);
    MoveFileExW(tmp, out, MOVEFILE_REPLACE_EXISTING);
}

/* The keys this file needs by value; written out here so no library of keys is needed. */
static const PROPERTYKEY KEY_FULL_DETAILS = { { 0xC9944A21, 0xA406, 0x48FE, { 0x82, 0x25, 0xAE, 0xC7, 0xE2, 0x4C, 0x21, 0x1B } }, 2 };
static const PROPERTYKEY KEY_RATING = { { 0x64440492, 0x4C8B, 0x11D1, { 0x8B, 0x70, 0x08, 0x00, 0x36, 0xB1, 0x1A, 0x03 } }, 9 };

/* The Details tab of the file: the type's FullDetails list, each property's words and value as Explorer shows them. */
static int props_read(const WCHAR *path, const WCHAR *list_file) {
    answer_t a = {0};
    IShellItem2 *item = NULL;
    IPropertyStore *store = NULL;
    IPropertyDescriptionList *list = NULL;
    LPWSTR names = NULL;
    HRESULT hr = SHCreateItemFromParsingName(path, NULL, &IID_IShellItem2, (void **)&item);
    hlog("item", hr);
    if (SUCCEEDED(hr)) hr = item->lpVtbl->GetPropertyStore(item, GPS_DEFAULT, &IID_IPropertyStore, (void **)&store);
    hlog("store", hr);
    if (SUCCEEDED(hr)) hr = item->lpVtbl->GetString(item, &KEY_FULL_DETAILS, &names);
    if (SUCCEEDED(hr)) hr = PSGetPropertyDescriptionListFromString(names, &IID_IPropertyDescriptionList, (void **)&list);
    hlog("list", hr);
    UINT count = 0;
    if (SUCCEEDED(hr)) list->lpVtbl->GetCount(list, &count);
    for (UINT i = 0; i < count; ++i) {
        IPropertyDescription *d = NULL;
        if (FAILED(list->lpVtbl->GetAt(list, i, &IID_IPropertyDescription, (void **)&d)) || !d) continue;
        PROPERTYKEY key;
        LPWSTR canon = NULL, label = NULL, shown = NULL;
        PROPDESC_TYPE_FLAGS tf = 0;
        d->lpVtbl->GetPropertyKey(d, &key);
        d->lpVtbl->GetCanonicalName(d, &canon);
        if (FAILED(d->lpVtbl->GetDisplayName(d, &label))) label = NULL;
        d->lpVtbl->GetTypeFlags(d, PDTF_MASK_ALL, &tf);
        if (tf & PDTF_ISGROUP) {
            answer_add(&a, "H\t", 2);
            answer_wide(&a, label ? label : canon);
            answer_add(&a, "\n", 1);
        } else {
            PROPVARIANT v;
            PropVariantInit(&v);
            bool rating = IsEqualPropertyKey(key, KEY_RATING);
            if (store && SUCCEEDED(store->lpVtbl->GetValue(store, &key, &v)) && v.vt != VT_EMPTY && !rating)
                d->lpVtbl->FormatForDisplay(d, &v, PDFF_DEFAULT, &shown);
            answer_add(&a, "P\t", 2);
            answer_wide(&a, canon);
            answer_add(&a, "\t", 1);
            answer_wide(&a, label ? label : canon);
            answer_add(&a, "\t", 1);
            if (!(tf & PDTF_ISINNATE)) answer_add(&a, "e", 1);
            if (tf & PDTF_MULTIPLEVALUES) answer_add(&a, "m", 1);
            if (rating) answer_add(&a, "r", 1);
            answer_add(&a, "\t", 1);
            if (rating) {
                char n[16];
                uint32_t r = v.vt == VT_UI4 ? v.ulVal : v.vt == VT_UI2 ? v.uiVal : 0;
                int len = rubraview_fmt(n, sizeof n, "{}", PROVEN_ARG(r));
                if (len > 0) answer_add(&a, n, (size_t)len);
            } else {
                answer_wide(&a, shown);
            }
            answer_add(&a, "\n", 1);
            PropVariantClear(&v);
        }
        CoTaskMemFree(canon);
        CoTaskMemFree(label);
        CoTaskMemFree(shown);
        d->lpVtbl->Release(d);
    }
    if (list) list->lpVtbl->Release(list);
    CoTaskMemFree(names);
    if (store) store->lpVtbl->Release(store);
    if (item) item->lpVtbl->Release(item);
    hlog("rows", count);
    if (a.len == 0) answer_add(&a, "", 0);
    answer_put(list_file, &a);
    rubraview_free(a.buf);
    return SUCCEEDED(hr) ? 0 : 10;
}

static WCHAR *dup_wide(const WCHAR *text, size_t n) {
    WCHAR *w = (WCHAR *)CoTaskMemAlloc((n + 1) * sizeof(WCHAR));
    if (!w) return NULL;
    proven_sys_mem_copy(w, text, n * sizeof(WCHAR));
    w[n] = 0;
    return w;
}

/* A typed value as the property's own: text, several texts split at ';', or empty (taken away). */
static HRESULT value_of(IPropertyDescription *d, const WCHAR *text, bool multi, PROPVARIANT *v) {
    PropVariantInit(v);
    size_t n = rubraview_wlen(text);
    while (n > 0 && text[n - 1] == L' ') n--;
    if (n == 0) return S_OK;   /* VT_EMPTY: the property removed */
    if (multi) {
        size_t parts = 1;
        for (size_t i = 0; i < n; ++i) if (text[i] == L';') parts++;
        LPWSTR *elems = (LPWSTR *)CoTaskMemAlloc(parts * sizeof(LPWSTR));
        if (!elems) return E_OUTOFMEMORY;
        size_t k = 0, from = 0;
        for (size_t i = 0; i <= n; ++i) {
            if (i < n && text[i] != L';') continue;
            size_t a = from, b = i;
            while (a < b && text[a] == L' ') a++;
            while (b > a && text[b - 1] == L' ') b--;
            if (b > a) elems[k++] = dup_wide(text + a, b - a);
            from = i + 1;
        }
        v->vt = VT_VECTOR | VT_LPWSTR;
        v->calpwstr.cElems = (ULONG)k;
        v->calpwstr.pElems = elems;
    } else {
        v->vt = VT_LPWSTR;
        v->pwszVal = dup_wide(text, n);
        if (!v->pwszVal) return E_OUTOFMEMORY;
    }
    return d->lpVtbl->CoerceToCanonicalValue(d, v);
}

/* The changes the viewer asked, on every file; one answer line a change. */
static int props_write(WCHAR **paths, size_t count, const WCHAR *list_file) {
    WCHAR in[MAX_PATH * 2 + 8];
    answer_t a = {0};
    if (!rubraview_wcopy(in, MAX_PATH * 2 + 8, list_file) || !rubraview_wcat(in, MAX_PATH * 2 + 8, L".in")) return 11;
    char *input = NULL;
    {
        HANDLE f = CreateFileW(in, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
        LARGE_INTEGER size = {0};
        DWORD got = 0;
        if (f != INVALID_HANDLE_VALUE && GetFileSizeEx(f, &size) && size.QuadPart < 4 * 1024 * 1024 &&
            (input = (char *)rubraview_alloc((size_t)size.QuadPart + 1)) != NULL && ReadFile(f, input, (DWORD)size.QuadPart, &got, NULL))
            input[got] = '\0';
        if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
        DeleteFileW(in);
    }
    if (!input) return 11;
    static rubraview_props_row_t rows[128];
    size_t rows_n = rubraview_props_changes_read(proven_u8str_view_from_cstr(input), rows, 128);
    rubraview_free(input);
    int code = 0;
    for (size_t f = 0; f < count; ++f) {
        IPropertyStore *store = NULL;
        HRESULT hr = SHGetPropertyStoreFromParsingName(paths[f], NULL, GPS_READWRITE, &IID_IPropertyStore, (void **)&store);
        hlog("store rw", hr);
        bool any = false;
        for (size_t r = 0; r < rows_n; ++r) {
            WCHAR name[96], value[600];
            MultiByteToWideChar(CP_UTF8, 0, rows[r].name, -1, name, 96);
            MultiByteToWideChar(CP_UTF8, 0, rows[r].value, -1, value, 600);
            PROPERTYKEY key;
            IPropertyDescription *d = NULL;
            HRESULT one = FAILED(hr) ? hr : PSGetPropertyKeyFromName(name, &key);
            if (SUCCEEDED(one)) one = PSGetPropertyDescription(&key, &IID_IPropertyDescription, (void **)&d);
            PROPDESC_TYPE_FLAGS tf = 0;
            if (SUCCEEDED(one)) d->lpVtbl->GetTypeFlags(d, PDTF_MASK_ALL, &tf);
            PROPVARIANT v;
            PropVariantInit(&v);
            if (SUCCEEDED(one)) one = value_of(d, value, (tf & PDTF_MULTIPLEVALUES) != 0, &v);
            if (SUCCEEDED(one)) one = store->lpVtbl->SetValue(store, &key, &v);
            if (SUCCEEDED(one)) any = true;
            PropVariantClear(&v);
            if (d) d->lpVtbl->Release(d);
            char line[200];
            int n = SUCCEEDED(one) ? rubraview_fmt(line, sizeof line, "ok\t{}\n", PROVEN_ARG(rows[r].name))
                                   : rubraview_fmt(line, sizeof line, "fail\t{}\t{:x}\n", PROVEN_ARG(rows[r].name), PROVEN_ARG((uint32_t)one));
            if (n > 0) answer_add(&a, line, (size_t)n < sizeof line ? (size_t)n : sizeof line - 1);
            if (FAILED(one)) code = 12;
        }
        if (store && any) {
            HRESULT c = store->lpVtbl->Commit(store);
            hlog("commit", c);
            if (FAILED(c)) {
                char line[64];
                int n = rubraview_fmt(line, sizeof line, "fail\tcommit\t{:x}\n", PROVEN_ARG((uint32_t)c));
                if (n > 0) answer_add(&a, line, (size_t)n);
                code = 13;
            }
        }
        if (store) store->lpVtbl->Release(store);
    }
    answer_add(&a, "", 0);
    answer_put(list_file, &a);
    rubraview_free(a.buf);
    return code;
}

/* ---- RV-281 phase 2: the elevated child's work, and a signature's certificate ---- */

static char *read_in(const WCHAR *list_file) {
    WCHAR in[MAX_PATH * 2 + 8];
    if (!rubraview_wcopy(in, MAX_PATH * 2 + 8, list_file) || !rubraview_wcat(in, MAX_PATH * 2 + 8, L".in")) return NULL;
    char *input = NULL;
    HANDLE f = CreateFileW(in, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
    LARGE_INTEGER size = {0};
    DWORD got = 0;
    if (f != INVALID_HANDLE_VALUE && GetFileSizeEx(f, &size) && size.QuadPart < 4 * 1024 * 1024 &&
        (input = (char *)rubraview_alloc((size_t)size.QuadPart + 1)) != NULL && ReadFile(f, input, (DWORD)size.QuadPart, &got, NULL))
        input[got] = '\0';
    if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
    DeleteFileW(in);
    if (!input) input = (char *)rubraview_alloc_zero(1, 1);   /* nothing asked: an empty request */
    return input;
}

static void answer_sec(answer_t *a, const rubraview_props_sec_t *sec, const rubraview_props_ace_t *aces, size_t n) {
    size_t cap = 4096 + n * 1200;
    char *text = (char *)rubraview_alloc(cap);
    if (text && rubraview_props_sec_lines(sec, aces, n, text, cap)) answer_add(a, text, proven_cstr_len(text));
    rubraview_free(text);
}

#define SEC_ACES_MAX 256

/* sec-write: the owner and the permissions the viewer could not write, written here as administrator. */
static int sec_write(const WCHAR *path, const WCHAR *list_file) {
    char *input = read_in(list_file);
    if (!input) return 11;
    static rubraview_props_ace_t aces[SEC_ACES_MAX];
    rubraview_props_sec_t change;
    size_t n = rubraview_props_sec_read(proven_u8str_view_from_cstr(input), &change, aces, SEC_ACES_MAX);
    rubraview_free(input);
    char path8[2048];
    WideCharToMultiByte(CP_UTF8, 0, path, -1, path8, sizeof path8, NULL, NULL);
    rubraview_props_sec_t answer = { .protect = -1 };
    if (!rubraview_pal_fileprops_security_write(path8, &change, aces, n, change.write_dacl, answer.error, sizeof answer.error) && !answer.error[0])
        (void)rubraview_fmt(answer.error, sizeof answer.error, "failed");
    hlog("sec-write", answer.error[0] ? 1 : 0);
    answer_t a = {0};
    answer_sec(&a, &answer, NULL, 0);
    answer_add(&a, "", 0);
    answer_put(list_file, &a);
    rubraview_free(a.buf);
    return answer.error[0] ? 20 : 0;
}

/* audit-read: the audit entries, and whether they inherit; audit-write: the entries given, written. */
static int audit_rw(const WCHAR *path, const WCHAR *list_file, bool write) {
    static rubraview_props_ace_t aces[SEC_ACES_MAX];
    char path8[2048];
    WideCharToMultiByte(CP_UTF8, 0, path, -1, path8, sizeof path8, NULL, NULL);
    rubraview_props_sec_t answer = { .protect = -1 };
    size_t n = 0;
    if (write) {
        char *input = read_in(list_file);
        if (!input) return 11;
        rubraview_props_sec_t change;
        n = rubraview_props_sec_read(proven_u8str_view_from_cstr(input), &change, aces, SEC_ACES_MAX);
        rubraview_free(input);
        if (!rubraview_pal_fileprops_audit_write(path8, aces, n, change.protect == 1, answer.error, sizeof answer.error) && !answer.error[0])
            (void)rubraview_fmt(answer.error, sizeof answer.error, "failed");
        n = 0;
    } else {
        bool protect = false;
        n = rubraview_pal_fileprops_audit_read(path8, aces, SEC_ACES_MAX, &protect, answer.error, sizeof answer.error);
        if (!answer.error[0]) answer.protect = protect ? 1 : 0;
    }
    hlog(write ? "audit-write" : "audit-read", (long long)n);
    answer_t a = {0};
    answer_sec(&a, &answer, aces, n);
    answer_add(&a, "", 0);
    answer_put(list_file, &a);
    rubraview_free(a.buf);
    return answer.error[0] ? 21 : 0;
}

/* certificate:<n>: Windows' own certificate window for the file's signature n (0 the primary). */
static int show_certificate(const WCHAR *path, int index, int32_t x, int32_t y) {
    HCERTSTORE store = NULL;
    HCRYPTMSG msg = NULL;
    DWORD enc = 0, content = 0, format = 0;
    if (!CryptQueryObject(CERT_QUERY_OBJECT_FILE, path, CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED_EMBED, CERT_QUERY_FORMAT_FLAG_BINARY, 0,
                          &enc, &content, &format, &store, &msg, NULL))
        return 16;
    HCRYPTMSG use = msg, inner = NULL;
    HCERTSTORE use_store = store, inner_store = NULL;
    DWORD n = 0;
    CMSG_SIGNER_INFO *si = NULL;
    if (CryptMsgGetParam(msg, CMSG_SIGNER_INFO_PARAM, 0, NULL, &n) && (si = (CMSG_SIGNER_INFO *)rubraview_alloc(n)) != NULL &&
        CryptMsgGetParam(msg, CMSG_SIGNER_INFO_PARAM, 0, si, &n) && index > 0) {
        for (DWORD i = 0; i < si->UnauthAttrs.cAttr; ++i) {
            CRYPT_ATTRIBUTE *at = &si->UnauthAttrs.rgAttr[i];
            if (!proven_u8str_view_eq(proven_u8str_view_from_cstr(at->pszObjId), PROVEN_LIT("1.3.6.1.4.1.311.2.4.1")) || (DWORD)index - 1 >= at->cValue) continue;
            inner = CryptMsgOpenToDecode(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0, 0, 0, NULL, NULL);
            if (inner && CryptMsgUpdate(inner, at->rgValue[index - 1].pbData, at->rgValue[index - 1].cbData, TRUE)) {
                use = inner;
                inner_store = CertOpenStore(CERT_STORE_PROV_MSG, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0, 0, inner);
                if (inner_store) use_store = inner_store;
            }
            break;
        }
    }
    rubraview_free(si);
    si = NULL;
    int code = 17;
    if (CryptMsgGetParam(use, CMSG_SIGNER_INFO_PARAM, 0, NULL, &n) && (si = (CMSG_SIGNER_INFO *)rubraview_alloc(n)) != NULL &&
        CryptMsgGetParam(use, CMSG_SIGNER_INFO_PARAM, 0, si, &n)) {
        CERT_INFO ci = { .Issuer = si->Issuer, .SerialNumber = si->SerialNumber };
        PCCERT_CONTEXT cert = CertFindCertificateInStore(use_store, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0, CERT_FIND_SUBJECT_CERT, &ci, NULL);
        if (cert) {
            HWND hwnd = make_window(x, y);
            SetForegroundWindow(hwnd);
            CRYPTUI_VIEWCERTIFICATE_STRUCTW view = { .dwSize = sizeof view, .hwndParent = hwnd, .pCertContext = cert };
            BOOL changed = FALSE;
            hlog("CryptUIDlgViewCertificateW", CryptUIDlgViewCertificateW(&view, &changed));
            if (hwnd) DestroyWindow(hwnd);
            CertFreeCertificateContext(cert);
            code = 0;
        }
    }
    rubraview_free(si);
    if (inner_store) CertCloseStore(inner_store, 0);
    if (inner) CryptMsgClose(inner);
    CryptMsgClose(msg);
    CertCloseStore(store, 0);
    return code;
}

/* Windows' own properties of the files, or its Open with box, then stay while it is up. */
static int show_windows_box(const WCHAR *kind, WCHAR **paths, size_t count, int32_t x, int32_t y) {
    HWND hwnd = make_window(x, y);
    hlog("foreground at once", SetForegroundWindow(hwnd));
    hold_process();
    int code = 0;
    if (rubraview_wcmp(kind, L"openwith") == 0) {
        OPENASINFO oai = { .pcszFile = paths[0], .oaifInFlags = OAIF_ALLOW_REGISTRATION | OAIF_REGISTER_EXT | OAIF_EXEC };
        HRESULT hr = SHOpenWithDialog(hwnd, &oai);
        hlog("SHOpenWithDialog", hr);
        if (FAILED(hr) && hr != HRESULT_FROM_WIN32(ERROR_CANCELLED)) code = 14;
    } else if (count == 1) {
        BOOL ok = SHObjectProperties(hwnd, SHOP_FILEPATH, paths[0], NULL);
        hlog("SHObjectProperties", ok);
        if (!ok) code = 15;
    } else {
        items_t it;
        IDataObject *data = NULL;
        if (items_make(&it, paths, count) &&
            SUCCEEDED(it.parent->lpVtbl->GetUIObjectOf(it.parent, hwnd, it.count, it.child, &IID_IDataObject, NULL, (void **)&data)) && data) {
            HRESULT hr = SHMultiFileProperties(data, 0);
            hlog("SHMultiFileProperties", hr);
            if (FAILED(hr)) code = 15;
            data->lpVtbl->Release(data);
        } else {
            code = 15;
        }
        items_free(&it);
    }
    linger(hwnd);
    if (hwnd) DestroyWindow(hwnd);
    return code;
}

int rubraview_pal_shellmenu_host_main(const wchar_t *kind, const wchar_t *list_file, const wchar_t *x, const wchar_t *y) {
    hlog_open();
    if (FAILED(OleInitialize(NULL))) { hlog("OleInitialize failed", 0); return 2; }   /* Copy and Cut put the files on the clipboard through OLE */
    WCHAR **paths = NULL;
    size_t count = read_list(list_file, &paths);
    hlog("files", count);
    int32_t px = (int32_t)StrToIntW(x), py = (int32_t)StrToIntW(y);
    int code = 1;
    if (count > 0) {
        if (rubraview_wcmp_n(kind, L"verb:", 5, false) == 0) code = run_verb(kind + 5, paths, count, px, py);
        else if (rubraview_wcmp(kind, L"props-read") == 0) code = props_read(paths[0], list_file);   /* RV-281 */
        else if (rubraview_wcmp(kind, L"props-write") == 0) code = props_write(paths, count, list_file);
        else if (rubraview_wcmp(kind, L"properties") == 0 || rubraview_wcmp(kind, L"openwith") == 0) code = show_windows_box(kind, paths, count, px, py);
        else if (rubraview_wcmp(kind, L"sec-write") == 0) code = sec_write(paths[0], list_file);   /* RV-281 phase 2: elevated */
        else if (rubraview_wcmp(kind, L"audit-read") == 0) code = audit_rw(paths[0], list_file, false);
        else if (rubraview_wcmp(kind, L"audit-write") == 0) code = audit_rw(paths[0], list_file, true);
        else if (rubraview_wcmp_n(kind, L"certificate:", 12, false) == 0) code = show_certificate(paths[0], StrToIntW(kind + 12), px, py);
        else code = show_menu(kind, paths, count, px, py);
    }
    for (size_t i = 0; i < count; ++i) rubraview_free(paths[i]);
    rubraview_free(paths);
    OleUninitialize();
    hlog("end", code);
    if (g_log != INVALID_HANDLE_VALUE) CloseHandle(g_log);
    return code;
}

#endif /* _WIN32 */
