/* RV-258 (D-310): the file system rubraview_fileop works through, on Windows — the primitives rubraview's own file
   manager begun before chose (../rubraman, src/pal/win32/rubraman_pal_win32.c): CopyFileExW that never overwrites,
   MoveFileExW never over a name, ReplaceFileW keeping what it replaced (then recycled), identity by volume serial and
   file id, FlushFileBuffers before a source is let go. */
#include <stdatomic.h>
#include "rubraview/core.h"
#include "rubraview/fileop.h"
#include "rubraview/pal/pal_fs.h"
#include <windows.h>

#define WPATH 4096

static bool wide(const char *utf8, WCHAR *out) {
    if (!utf8 || !*utf8) return false;
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, out, WPATH);
    if (n <= 0) return false;
    for (int i = 0; out[i]; ++i) if (out[i] == L'/') out[i] = L'\\';
    return true;
}

static bool w_stat(void *ctx, const char *path, rubraview_fileop_stat_t *o) {
    (void)ctx;
    *o = (rubraview_fileop_stat_t){ 0 };
    WCHAR p[WPATH];
    if (!wide(path, p)) return false;
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExW(p, GetFileExInfoStandard, &a)) return false;
    o->exists = true;
    o->is_dir = (a.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    o->is_link = (a.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
    o->size = ((uint64_t)a.nFileSizeHigh << 32) | a.nFileSizeLow;
    ULARGE_INTEGER t = { .LowPart = a.ftLastWriteTime.dwLowDateTime, .HighPart = a.ftLastWriteTime.dwHighDateTime };
    o->mtime = (int64_t)(t.QuadPart / 10000000ull) - 11644473600ll;
    /* who it is: the volume and the file id (a hard link is the same file) */
    HANDLE h = CreateFileW(p, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                           FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        BY_HANDLE_FILE_INFORMATION info;
        if (GetFileInformationByHandle(h, &info)) {
            o->volume = info.dwVolumeSerialNumber;
            o->file_id = ((uint64_t)info.nFileIndexHigh << 32) | info.nFileIndexLow;
        }
        CloseHandle(h);
    }
    return true;
}

static bool w_list(void *ctx, const char *dir, void (*each)(void *user, const char *name), void *user) {
    (void)ctx;
    WCHAR p[WPATH];
    if (!wide(dir, p)) return false;
    size_t n = 0;
    while (p[n]) ++n;
    if (n + 3 >= WPATH) return false;
    if (p[n - 1] != L'\\') p[n++] = L'\\';
    p[n++] = L'*';
    p[n] = 0;
    WIN32_FIND_DATAW f;
    HANDLE h = FindFirstFileExW(p, FindExInfoBasic, &f, FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_FILE_NOT_FOUND;
    /* the names first, then given: `each` may grow the operation while the search is open */
    size_t cap = 64, count = 0;
    char **names = (char **)rubraview_alloc_zero(cap, sizeof *names);
    bool ok = names != NULL;
    do {
        if (!ok) break;
        if (f.cFileName[0] == L'.' && (f.cFileName[1] == 0 || (f.cFileName[1] == L'.' && f.cFileName[2] == 0))) continue;
        char utf8[WPATH * 3];
        if (WideCharToMultiByte(CP_UTF8, 0, f.cFileName, -1, utf8, (int)sizeof utf8, NULL, NULL) <= 0) { ok = false; break; }
        if (count == cap) {
            char **grown = (char **)rubraview_alloc_zero(cap * 2, sizeof *grown);
            if (!grown) { ok = false; break; }
            proven_sys_mem_copy(grown, names, count * sizeof *names);
            rubraview_free(names);
            names = grown;
            cap *= 2;
        }
        names[count] = rubraview_strdup(utf8);
        if (!names[count]) { ok = false; break; }
        count++;
    } while (FindNextFileW(h, &f));
    FindClose(h);
    for (size_t i = 0; i < count; ++i) { if (ok) each(user, names[i]); rubraview_free(names[i]); }
    rubraview_free(names);
    return ok;
}

/* RV-269: the file in hand's bytes so far, for the progress card */
static DWORD CALLBACK w_copy_progress(LARGE_INTEGER total, LARGE_INTEGER moved, LARGE_INTEGER stream_total, LARGE_INTEGER stream_moved,
                                      DWORD stream, DWORD reason, HANDLE from, HANDLE to, LPVOID data) {
    (void)total; (void)stream_total; (void)stream_moved; (void)stream; (void)reason; (void)from; (void)to;
    atomic_store((_Atomic uint64_t *)data, (uint64_t)moved.QuadPart);
    return PROGRESS_CONTINUE;
}

static bool w_copy(void *ctx, const char *from, const char *to) {
    WCHAR a[WPATH], b[WPATH];
    if (!wide(from, a) || !wide(to, b)) return false;
    _Atomic uint64_t *copied = (_Atomic uint64_t *)ctx;
    BOOL ok = CopyFileExW(a, b, copied ? w_copy_progress : NULL, copied, NULL, COPY_FILE_FAIL_IF_EXISTS);
    if (copied) atomic_store(copied, 0);
    if (!ok) return false;
    /* CopyFileExW carries the read-only flag over; a read-only staging file could not be swapped in or removed if
       the copy were given up (rubraman learnt it), so the copy is left writable */
    DWORD at = GetFileAttributesW(b);
    if (at != INVALID_FILE_ATTRIBUTES && (at & FILE_ATTRIBUTE_READONLY)) (void)SetFileAttributesW(b, at & ~(DWORD)FILE_ATTRIBUTE_READONLY);
    return true;
}

static bool w_rename(void *ctx, const char *from, const char *to) {
    (void)ctx;
    WCHAR a[WPATH], b[WPATH];
    if (!wide(from, a) || !wide(to, b)) return false;
    return MoveFileExW(a, b, 0) != 0;   /* never over a name, never across volumes */
}

static bool w_recycle(void *ctx, const char *path) {
    (void)ctx;
    return rubraview_pal_fs_recycle(proven_u8str_view_from_cstr(path));
}

static bool w_replace(void *ctx, const char *target, const char *staged) {
    (void)ctx;
    WCHAR t[WPATH], s[WPATH], kept[WPATH];
    if (!wide(target, t) || !wide(staged, s)) return false;
    /* what it replaces is kept under a name beside it, then recycled: the undo can find it in the Recycle Bin */
    size_t n = 0;
    while (s[n]) ++n;
    if (n + 6 >= WPATH) return false;
    proven_sys_mem_copy(kept, s, (n + 1) * sizeof(WCHAR));
    kept[n] = L'.'; kept[n + 1] = L'o'; kept[n + 2] = L'l'; kept[n + 3] = L'd'; kept[n + 4] = 0;
    if (!ReplaceFileW(t, s, kept, REPLACEFILE_IGNORE_MERGE_ERRORS, NULL, NULL)) return false;
    char utf8[WPATH * 3];
    if (WideCharToMultiByte(CP_UTF8, 0, kept, -1, utf8, (int)sizeof utf8, NULL, NULL) > 0 &&
        !rubraview_pal_fs_recycle(proven_u8str_view_from_cstr(utf8)))
        (void)DeleteFileW(kept);   /* the Recycle Bin would not take it (another drive's, too large): the swap stands */
    return true;
}

static bool w_make_dir(void *ctx, const char *path) {
    (void)ctx;
    WCHAR p[WPATH];
    return wide(path, p) && CreateDirectoryW(p, NULL) != 0;
}

static bool w_remove_empty_dir(void *ctx, const char *path) {
    (void)ctx;
    WCHAR p[WPATH];
    return wide(path, p) && RemoveDirectoryW(p) != 0;   /* fails when anything is in it */
}

static bool w_remove(void *ctx, const char *path) {
    (void)ctx;
    WCHAR p[WPATH];
    return wide(path, p) && DeleteFileW(p) != 0;
}

static int w_same_bytes(void *ctx, const char *a, const char *b) {
    (void)ctx;
    WCHAR x[WPATH], y[WPATH];
    if (!wide(a, x) || !wide(b, y)) return -1;
    HANDLE hx = CreateFileW(x, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (hx == INVALID_HANDLE_VALUE) return -1;
    HANDLE hy = CreateFileW(y, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (hy == INVALID_HANDLE_VALUE) { CloseHandle(hx); return -1; }
    int same = -1;
    LARGE_INTEGER sx, sy;
    enum { CHUNK = 1 << 20 };
    unsigned char *bx = (unsigned char *)rubraview_alloc(CHUNK), *by = (unsigned char *)rubraview_alloc(CHUNK);
    if (bx && by && GetFileSizeEx(hx, &sx) && GetFileSizeEx(hy, &sy)) {
        same = sx.QuadPart == sy.QuadPart ? 1 : 0;
        while (same == 1) {
            DWORD rx = 0, ry = 0;
            if (!ReadFile(hx, bx, CHUNK, &rx, NULL) || !ReadFile(hy, by, CHUNK, &ry, NULL)) { same = -1; break; }
            if (rx != ry) { same = 0; break; }
            if (rx == 0) break;
            if (proven_sys_mem_cmp(bx, by, rx) != 0) same = 0;
        }
    }
    rubraview_free(bx);
    rubraview_free(by);
    CloseHandle(hx);
    CloseHandle(hy);
    return same;
}

static bool w_flush(void *ctx, const char *path) {
    (void)ctx;
    WCHAR p[WPATH];
    if (!wide(path, p)) return false;
    HANDLE h = CreateFileW(p, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    bool ok = FlushFileBuffers(h) != 0;
    CloseHandle(h);
    return ok;
}

/* The volume of a path, or of its nearest folder that is there (a destination not made yet). */
static bool volume_of(const char *path, DWORD *serial) {
    WCHAR p[WPATH], root[MAX_PATH + 1];
    if (!wide(path, p)) return false;
    if (!GetVolumePathNameW(p, root, MAX_PATH)) return false;
    return GetVolumeInformationW(root, NULL, 0, serial, NULL, NULL, NULL, 0) != 0;
}

static bool w_same_volume(void *ctx, const char *a, const char *b) {
    (void)ctx;
    DWORD x = 0, y = 0;
    return volume_of(a, &x) && volume_of(b, &y) && x == y;
}

static const rubraview_fileop_fs_t W_FS = {
    .ctx = NULL, .stat = w_stat, .list = w_list, .copy = w_copy, .rename = w_rename, .replace = w_replace,
    .recycle = w_recycle, .make_dir = w_make_dir, .remove_empty_dir = w_remove_empty_dir, .remove = w_remove,
    .same_bytes = w_same_bytes, .flush = w_flush, .same_volume = w_same_volume,
};

const struct rubraview_fileop_fs *rubraview_pal_fileop_fs(void) { return &W_FS; }
