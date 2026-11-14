/*
 * RV-281: a file's General tab through Windows' system calls (pal_fileprops.h): attributes, sizes, times, its type and
 * program from the registry (AssocQueryString), whether it came from another computer, and the changes General makes.
 */
#ifdef _WIN32
#include "rubraview/pal/pal_fileprops.h"
#include "rubraview/pal/pal_wide.h"

#include <windows.h>
#include <shlwapi.h>
#include <winioctl.h>

#define WPATH 2048

static bool to_wide(const char *text, WCHAR *out, int cap) {
    return MultiByteToWideChar(CP_UTF8, 0, text, -1, out, cap) > 0;
}

static void to_utf8(const WCHAR *text, char *out, size_t cap) {
    if (WideCharToMultiByte(CP_UTF8, 0, text, -1, out, (int)cap, NULL, NULL) <= 0 && cap) out[0] = '\0';
}

static int64_t unix_of(FILETIME ft) {
    ULARGE_INTEGER u = { .LowPart = ft.dwLowDateTime, .HighPart = ft.dwHighDateTime };
    if (u.QuadPart == 0) return 0;
    return (int64_t)(u.QuadPart / 10000000ULL) - 11644473600LL;
}

static uint32_t rv_attrs(DWORD a) {
    uint32_t r = 0;
    if (a & FILE_ATTRIBUTE_READONLY) r |= RUBRAVIEW_FATTR_READONLY;
    if (a & FILE_ATTRIBUTE_HIDDEN) r |= RUBRAVIEW_FATTR_HIDDEN;
    if (a & FILE_ATTRIBUTE_ARCHIVE) r |= RUBRAVIEW_FATTR_ARCHIVE;
    if (!(a & FILE_ATTRIBUTE_NOT_CONTENT_INDEXED)) r |= RUBRAVIEW_FATTR_INDEXED;
    if (a & FILE_ATTRIBUTE_COMPRESSED) r |= RUBRAVIEW_FATTR_COMPRESSED;
    if (a & FILE_ATTRIBUTE_ENCRYPTED) r |= RUBRAVIEW_FATTR_ENCRYPTED;
    return r;
}

/* The drive's cluster, for the size on disk. */
static DWORD cluster_of(const WCHAR *path) {
    WCHAR root[MAX_PATH];
    if (!GetVolumePathNameW(path, root, MAX_PATH)) return 4096;
    DWORD spc = 0, bps = 0, free_c = 0, total = 0;
    if (!GetDiskFreeSpaceW(root, &spc, &bps, &free_c, &total) || spc * bps == 0) return 4096;
    return spc * bps;
}

static uint64_t on_disk_of(const WCHAR *path, uint64_t size, DWORD cluster) {
    DWORD high = 0, low = GetCompressedFileSizeW(path, &high);
    uint64_t real = (low == INVALID_FILE_SIZE && GetLastError() != NO_ERROR) ? size : ((uint64_t)high << 32) | low;
    return (real + cluster - 1) / cluster * cluster;
}

typedef struct counter { uint64_t size, on_disk; uint32_t files, folders; ULONGLONG until; bool stopped; DWORD cluster; } counter_t;

static void count_folder(counter_t *c, WCHAR *path, size_t len, int depth) {
    if (depth > 64 || c->stopped) return;
    if (len + 3 >= WPATH) return;
    path[len] = L'\\'; path[len + 1] = L'*'; path[len + 2] = 0;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW(path, FindExInfoBasic, &fd, FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH);
    path[len] = 0;
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (GetTickCount64() > c->until) { c->stopped = true; break; }
        if (fd.cFileName[0] == L'.' && (fd.cFileName[1] == 0 || (fd.cFileName[1] == L'.' && fd.cFileName[2] == 0))) continue;
        size_t n = rubraview_wlen(fd.cFileName);
        if (len + 1 + n + 1 >= WPATH) continue;
        path[len] = L'\\';
        proven_sys_mem_copy(path + len + 1, fd.cFileName, (n + 1) * sizeof(WCHAR));
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            c->folders++;
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) count_folder(c, path, len + 1 + n, depth + 1);
        } else {
            uint64_t size = ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
            c->files++;
            c->size += size;
            c->on_disk += on_disk_of(path, size, c->cluster);
        }
        path[len] = 0;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

bool rubraview_pal_fileprops_read(const char *path, rubraview_fileprops_file_t *out, uint32_t budget_ms) {
    static WCHAR w[WPATH];
    *out = (rubraview_fileprops_file_t){0};
    if (!path || !to_wide(path, w, WPATH)) return false;
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (!GetFileAttributesExW(w, GetFileExInfoStandard, &fa)) return false;
    (void)rubraview_fmt(out->path, sizeof out->path, "{}", PROVEN_ARG(path));
    out->is_folder = (fa.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    out->attributes = rv_attrs(fa.dwFileAttributes);
    out->created = unix_of(fa.ftCreationTime);
    out->modified = unix_of(fa.ftLastWriteTime);
    out->accessed = unix_of(fa.ftLastAccessTime);
    DWORD cluster = cluster_of(w);
    if (out->is_folder) {
        counter_t c = { .until = GetTickCount64() + budget_ms, .cluster = cluster };
        size_t len = rubraview_wlen(w);
        while (len > 3 && w[len - 1] == L'\\') w[--len] = 0;
        count_folder(&c, w, len, 0);
        out->size = c.size; out->on_disk = c.on_disk; out->files = c.files; out->folders = c.folders;
        out->counted_all = !c.stopped;
    } else {
        out->size = ((uint64_t)fa.nFileSizeHigh << 32) | fa.nFileSizeLow;
        out->on_disk = on_disk_of(w, out->size, cluster);
        out->counted_all = true;
    }
    /* the type's words and its program, from the registry */
    WCHAR words[260];
    DWORD n = 260;
    const WCHAR *ext = out->is_folder ? L"Directory" : PathFindExtensionW(w);
    if (ext && *ext && SUCCEEDED(AssocQueryStringW(ASSOCF_INIT_IGNOREUNKNOWN, ASSOCSTR_FRIENDLYDOCNAME, ext, NULL, words, &n)))
        to_utf8(words, out->type_name, sizeof out->type_name);
    n = 260;
    if (!out->is_folder && ext && *ext && SUCCEEDED(AssocQueryStringW(ASSOCF_INIT_IGNOREUNKNOWN, ASSOCSTR_FRIENDLYAPPNAME, ext, NULL, words, &n)))
        to_utf8(words, out->opens_with, sizeof out->opens_with);
    /* it came from another computer */
    if (!out->is_folder) {
        static WCHAR zone[WPATH + 32];
        if (rubraview_wcopy(zone, WPATH + 32, w) && rubraview_wcat(zone, WPATH + 32, L":Zone.Identifier")) {
            HANDLE h = CreateFileW(zone, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
            if (h != INVALID_HANDLE_VALUE) { out->blocked = true; CloseHandle(h); }
        }
    }
    /* what the drive and the system can do */
    WCHAR root[MAX_PATH];
    DWORD flags = 0;
    if (GetVolumePathNameW(w, root, MAX_PATH) && GetVolumeInformationW(root, NULL, 0, NULL, NULL, &flags, NULL, 0)) {
        out->can_compress = (flags & FILE_FILE_COMPRESSION) != 0;
        out->can_encrypt = (flags & FILE_SUPPORTS_ENCRYPTION) != 0;
    }
    return true;
}

static bool fail(char *why, size_t cap) {
    DWORD err = GetLastError();
    WCHAR text[300];
    if (why && cap) {
        if (FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, err, 0, text, 300, NULL) == 0) text[0] = 0;
        size_t len = rubraview_wlen(text);
        while (len > 0 && (text[len - 1] == L'\n' || text[len - 1] == L'\r' || text[len - 1] == L' ')) text[--len] = 0;
        to_utf8(text, why, cap);
    }
    return false;
}

static bool set_compression(const WCHAR *w, bool on, bool folder) {
    HANDLE h = CreateFileW(w, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                           folder ? FILE_FLAG_BACKUP_SEMANTICS : 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    USHORT format = on ? COMPRESSION_FORMAT_DEFAULT : COMPRESSION_FORMAT_NONE;
    DWORD got = 0;
    BOOL ok = DeviceIoControl(h, FSCTL_SET_COMPRESSION, &format, sizeof format, NULL, 0, &got, NULL);
    DWORD err = GetLastError();
    CloseHandle(h);
    SetLastError(err);
    return ok != 0;
}

bool rubraview_pal_fileprops_set_attributes(const char *path, uint32_t have, uint32_t want, char *why, size_t cap) {
    static WCHAR w[WPATH];
    if (why && cap) why[0] = '\0';
    if (!to_wide(path, w, WPATH)) return false;
    DWORD a = GetFileAttributesW(w);
    if (a == INVALID_FILE_ATTRIBUTES) return fail(why, cap);
    bool folder = (a & FILE_ATTRIBUTE_DIRECTORY) != 0;
    /* first what is undone: encryption and compression exclude each other */
    if ((have & RUBRAVIEW_FATTR_ENCRYPTED) && !(want & RUBRAVIEW_FATTR_ENCRYPTED) && !DecryptFileW(w, 0)) return fail(why, cap);
    if ((have & RUBRAVIEW_FATTR_COMPRESSED) && !(want & RUBRAVIEW_FATTR_COMPRESSED) && !set_compression(w, false, folder)) return fail(why, cap);
    DWORD plain = a & ~(DWORD)(FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED |
                               FILE_ATTRIBUTE_COMPRESSED | FILE_ATTRIBUTE_ENCRYPTED | FILE_ATTRIBUTE_DIRECTORY);
    if (want & RUBRAVIEW_FATTR_READONLY) plain |= FILE_ATTRIBUTE_READONLY;
    if (want & RUBRAVIEW_FATTR_HIDDEN) plain |= FILE_ATTRIBUTE_HIDDEN;
    if (want & RUBRAVIEW_FATTR_ARCHIVE) plain |= FILE_ATTRIBUTE_ARCHIVE;
    if (!(want & RUBRAVIEW_FATTR_INDEXED)) plain |= FILE_ATTRIBUTE_NOT_CONTENT_INDEXED;
    if (plain == 0) plain = FILE_ATTRIBUTE_NORMAL;
    /* a read-only file is encrypted or compressed before it is made read-only */
    if (!SetFileAttributesW(w, plain & ~(DWORD)FILE_ATTRIBUTE_READONLY) && !SetFileAttributesW(w, plain)) return fail(why, cap);
    if (!(have & RUBRAVIEW_FATTR_COMPRESSED) && (want & RUBRAVIEW_FATTR_COMPRESSED) && !set_compression(w, true, folder)) return fail(why, cap);
    if (!(have & RUBRAVIEW_FATTR_ENCRYPTED) && (want & RUBRAVIEW_FATTR_ENCRYPTED) && !EncryptFileW(w)) return fail(why, cap);
    if ((plain & FILE_ATTRIBUTE_READONLY) && !SetFileAttributesW(w, plain)) return fail(why, cap);
    return true;
}

bool rubraview_pal_fileprops_unblock(const char *path) {
    static WCHAR file[WPATH], w[WPATH + 32];
    if (!to_wide(path, file, WPATH) || !rubraview_wcopy(w, WPATH + 32, file) || !rubraview_wcat(w, WPATH + 32, L":Zone.Identifier")) return false;
    /* a read-only file's streams are read-only too (VM: Access is denied): its mark taken off a moment for it */
    DWORD a = GetFileAttributesW(file);
    bool ro = a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_READONLY);
    if (ro) SetFileAttributesW(file, a & ~(DWORD)FILE_ATTRIBUTE_READONLY);
    bool ok = DeleteFileW(w) != 0 || GetLastError() == ERROR_FILE_NOT_FOUND;
    if (ro) SetFileAttributesW(file, a);
    return ok;
}

#endif /* _WIN32 */
