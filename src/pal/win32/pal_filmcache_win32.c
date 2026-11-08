/*
 * RV-216 (D-249): the network film cache on Windows (see pal_filmcache.h). A copy is a sparse file at the
 * source's own offsets; its map is saved beside it every 16 MB filled and when it is let go. A block is
 * written to the copy before its bit is set, and the bit is saved after, so a map never names bytes the
 * copy has not been given.
 */
#ifdef _WIN32
#define COBJMACROS
#include "rubraview/pal/pal_wide.h"
#include "rubraview/core.h"
#include <windows.h>
#include <objidl.h>
#include <shlobj.h>
#include <winioctl.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rubraview/pal/pal_filmcache.h"
#include "rubraview/filmcache.h"
#include "rubraview/pal/pal_media.h"

#define BLOCK RUBRAVIEW_FILMCACHE_BLOCK
#define SAVE_EVERY 64u        /* blocks filled between two saves of the map: 16 MB */
#define WPATH 2048
#define MAX_COPIES 4096       /* copies looked at for the limit */

struct rubraview_filmcache_stream {
    struct rubraview_filmcache_stream *next;
    LONG refs;                              /* under g_list_lock */
    char name[17];
    char path[WPATH * 3];                   /* UTF-8, for the map */
    size_t path_len;
    WCHAR bin[WPATH], map_file[WPATH];
    HANDLE source, ahead_source, copy;      /* copy NULL: read through, another process writes it */
    SRWLOCK lock;                           /* the map and saved */
    rubraview_filmcache_map_t map;
    uint64_t saved;                         /* map.filled when it was last saved */
    _Atomic uint64_t ahead_from;            /* the block after the last one read for playing */
    _Atomic bool closing;
    HANDLE ahead_thread;
    /* RV-261: the film in memory, read by its own thread from where it is played on */
    bool local;                             /* not on a network drive: read straight, counted apart */
    uint8_t *ram;                           /* ramw.slots blocks, on the heap */
    rubraview_filmbuf_t ramw;               /* under ram_lock */
    SRWLOCK ram_lock;
    HANDLE ram_source, ram_thread;
    _Atomic uint64_t ram_limit;             /* blocks memory reads at most from the start; UINT64_MAX all */
    _Atomic uint64_t read_at;               /* the block last read for playing */
    _Atomic bool ram_full_said;
};

/* RUBRAVIEW_FILMCACHE_LOG set: what each process does with the cache, a line at a time, in that file. */
static void say_args(const char *format, const proven_arg_t *fmt_args, size_t fmt_count) {
    WCHAR file[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"RUBRAVIEW_FILMCACHE_LOG", file, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return;
    char line[512];
    int head = rubraview_fmt(line, sizeof line, "{} ", PROVEN_ARG((unsigned long)GetCurrentProcessId()));
    int body = rubraview_fmt_length(proven_fmt_to_internal(line + head, sizeof line - (size_t)head - 1, format, fmt_args, fmt_count));
    if (head < 0 || body < 0) return;
    size_t len = proven_cstr_len(line);
    line[len++] = '\n';
    HANDLE h = CreateFileW(file, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD put = 0;
    WriteFile(h, line, (DWORD)len, &put, NULL);
    CloseHandle(h);
}
#define say(format, ...) say_args(format, RUBRAVIEW_FMT_ARGS(__VA_ARGS__))

static SRWLOCK g_cfg_lock = SRWLOCK_INIT;
static char g_folder_set[WPATH];
static uint64_t g_limit;
static bool g_ahead;

static SRWLOCK g_list_lock = SRWLOCK_INIT;
static rubraview_filmcache_stream_t *g_list, *g_last;
static _Atomic uint64_t g_from_copy, g_from_source, g_from_memory;
static _Atomic uint64_t g_memory_setting;   /* RV-261 */
static _Atomic uint64_t g_memory_held, g_memory_room;

void rubraview_pal_filmcache_configure_memory(uint64_t bytes) { atomic_store(&g_memory_setting, bytes); }
uint64_t rubraview_pal_filmcache_memory_setting(void) { return atomic_load(&g_memory_setting); }

void rubraview_pal_filmcache_configure(proven_u8str_view_t folder, uint64_t limit_bytes, bool ahead) {
    AcquireSRWLockExclusive(&g_cfg_lock);
    size_t n = folder.size < sizeof g_folder_set - 1 ? folder.size : 0;   /* a folder too long: the default */
    if (n) proven_sys_mem_copy(g_folder_set, folder.ptr, n);
    g_folder_set[n] = '\0';
    bool changed = g_limit != limit_bytes || g_ahead != ahead;
    g_limit = limit_bytes;
    g_ahead = ahead;
    ReleaseSRWLockExclusive(&g_cfg_lock);
    if (changed) say("settings: {} MB, ahead {}, folder \"{}\"", PROVEN_ARG((unsigned long long)(limit_bytes >> 20)), PROVEN_ARG(ahead ? "on" : "off"), PROVEN_ARG(g_folder_set));
}

void rubraview_pal_filmcache_settings(char *folder, size_t cap, uint64_t *limit_bytes, bool *ahead) {
    AcquireSRWLockShared(&g_cfg_lock);
    if (folder && cap) rubraview_fmt(folder, cap, "{}", PROVEN_ARG(g_folder_set));
    if (limit_bytes) *limit_bytes = g_limit;
    if (ahead) *ahead = g_ahead;
    ReleaseSRWLockShared(&g_cfg_lock);
}

/* The folder, wide; made if missing when `make`; false when there is none (or, not made, none yet). */
static bool folder_at(WCHAR *out, size_t cap, bool make) {
    out[0] = L'\0';
    char set[WPATH];
    rubraview_pal_filmcache_settings(set, sizeof set, NULL, NULL);
    if (set[0]) {
        if (MultiByteToWideChar(CP_UTF8, 0, set, -1, out, (int)cap) <= 0) return false;
    } else {
        WCHAR base[WPATH];
        DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, WPATH);
        if (n == 0 || n >= WPATH) return false;
        if (rubraview_wfmt(out, cap, "{}\\rubraview\\film-cache", PROVEN_ARG(rubraview_wide_view(base))) < 0) return false;
        out[cap - 1] = L'\0';
    }
    for (WCHAR *p = out; *p; ++p) if (*p == L'/') *p = L'\\';
    DWORD a = GetFileAttributesW(out);
    if (a != INVALID_FILE_ATTRIBUTES) return (a & FILE_ATTRIBUTE_DIRECTORY) != 0;
    if (!make) return false;
    int made = SHCreateDirectoryExW(NULL, out, NULL);
    return made == ERROR_SUCCESS || made == ERROR_ALREADY_EXISTS;
}

void rubraview_pal_filmcache_folder(char *out, size_t cap) {
    WCHAR w[WPATH];
    if (!out || cap == 0) return;
    out[0] = '\0';
    (void)folder_at(w, WPATH, false);   /* named even before the first copy makes it */
    if (w[0]) WideCharToMultiByte(CP_UTF8, 0, w, -1, out, (int)cap, NULL, NULL);
}

static bool on_network(const wchar_t *path) {
    if (path[0] == L'\\' && path[1] == L'\\') {
        if (path[2] != L'?' && path[2] != L'.') return true;   /* \\server\share */
        if (rubraview_wcmp_n(path, L"\\\\?\\UNC\\", 8, false) == 0) return true;
        if (path[2] == L'?' && path[3] == L'\\') path += 4;    /* \\?\C:\... */
        else return false;
    }
    if (!((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z')) || path[1] != L':') return false;
    WCHAR root[4] = { path[0], L':', L'\\', 0 };
    return GetDriveTypeW(root) == DRIVE_REMOTE;
}

static bool pread_at(HANDLE h, uint64_t at, void *dst, DWORD len, DWORD *got) {
    OVERLAPPED ov = { .Offset = (DWORD)at, .OffsetHigh = (DWORD)(at >> 32) };
    *got = 0;
    if (ReadFile(h, dst, len, got, &ov)) return true;
    return GetLastError() == ERROR_HANDLE_EOF;
}

static bool pwrite_at(HANDLE h, uint64_t at, const void *src, DWORD len) {
    OVERLAPPED ov = { .Offset = (DWORD)at, .OffsetHigh = (DWORD)(at >> 32) };
    DWORD put = 0;
    return WriteFile(h, src, len, &put, &ov) && put == len;
}

/* The map to its file: a new file put in place, so a save cut short leaves the old one. Under s->lock. */
static void save_map(rubraview_filmcache_stream_t *s) {
    if (!s->copy) return;
    proven_u8str_view_t path = { .ptr = (const proven_byte_t *)s->path, .size = s->path_len };
    size_t cap = rubraview_filmcache_map_text_size(&s->map, path);
    char *text = (char *)rubraview_alloc(cap);
    size_t n = text ? rubraview_filmcache_map_text(&s->map, path, text, cap) : 0;
    WCHAR tmp[WPATH + 8];
    rubraview_wfmt(tmp, WPATH + 8, "{}.new", PROVEN_ARG(rubraview_wide_view(s->map_file)));
    tmp[WPATH + 7] = L'\0';
    HANDLE h = n ? CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL) : INVALID_HANDLE_VALUE;
    if (h != INVALID_HANDLE_VALUE) {
        DWORD put = 0;
        bool ok = WriteFile(h, text, (DWORD)n, &put, NULL) && put == n;
        CloseHandle(h);
        if (ok && MoveFileExW(tmp, s->map_file, MOVEFILE_REPLACE_EXISTING)) s->saved = s->map.filled;
        else DeleteFileW(tmp);
    }
    rubraview_free(text);
}

/* Block `b` from the network into `buf`, then into the copy and the map; its length, 0 when it failed. */
static DWORD fetch_block(rubraview_filmcache_stream_t *s, HANDLE source, uint64_t b, uint8_t *buf) {
    uint64_t at = b * BLOCK;
    if (at >= s->map.size) return 0;
    DWORD want = s->map.size - at < BLOCK ? (DWORD)(s->map.size - at) : BLOCK, got = 0;
    if (!pread_at(source, at, buf, want, &got) || got != want) return 0;
    atomic_fetch_add(&g_from_source, got);
    if (s->copy && pwrite_at(s->copy, at, buf, got)) {
        AcquireSRWLockExclusive(&s->lock);
        rubraview_filmcache_map_mark(&s->map, b);
        if (s->map.filled - s->saved >= SAVE_EVERY || s->map.filled == s->map.blocks) save_map(s);
        ReleaseSRWLockExclusive(&s->lock);
    }
    return got;
}

static bool has_block(rubraview_filmcache_stream_t *s, uint64_t b) {
    if (!s->copy) return false;
    AcquireSRWLockShared(&s->lock);
    bool has = rubraview_filmcache_map_has(&s->map, b);
    ReleaseSRWLockShared(&s->lock);
    return has;
}

static DWORD WINAPI ahead_main(void *arg) {
    rubraview_filmcache_stream_t *s = (rubraview_filmcache_stream_t *)arg;
    uint8_t *buf = (uint8_t *)rubraview_alloc(BLOCK);
    if (!buf) return 0;
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    while (!atomic_load(&s->closing)) {
        AcquireSRWLockShared(&s->lock);
        uint64_t gap = rubraview_filmcache_map_next_gap(&s->map, atomic_load(&s->ahead_from));
        if (gap >= s->map.blocks) gap = rubraview_filmcache_map_next_gap(&s->map, 0);
        uint64_t blocks = s->map.blocks;
        ReleaseSRWLockShared(&s->lock);
        if (gap >= blocks) { say("ahead: all {} blocks here", PROVEN_ARG((unsigned long long)blocks)); break; }   /* all of it is here */
        if (!fetch_block(s, s->ahead_source, gap, buf)) Sleep(500);   /* the link dropped: again soon */
    }
    rubraview_free(buf);
    return 0;
}

uint64_t rubraview_pal_filmcache_size(const rubraview_filmcache_stream_t *s) {
    return s ? s->map.size : 0;
}

bool rubraview_pal_filmcache_read(rubraview_filmcache_stream_t *s, uint64_t at, proven_mem_mut_t dst_view, uint32_t *got) {
    void *dst = (void *)dst_view.ptr;
    uint32_t len = (uint32_t)dst_view.size;
    *got = 0;
    if (!s || !dst) return false;
    if (at >= s->map.size || len == 0) return true;
    if (len > s->map.size - at) len = (uint32_t)(s->map.size - at);
    uint8_t *out = (uint8_t *)dst, *buf = NULL;
    uint64_t pos = at;
    uint32_t left = len;
    bool ok = true;
    while (left > 0) {
        uint64_t b = pos / BLOCK;
        uint32_t off = (uint32_t)(pos % BLOCK), n = BLOCK - off < left ? BLOCK - off : left;
        DWORD took = 0;
        bool from_ram = false;
        if (s->ram) {   /* RV-261: memory first */
            AcquireSRWLockShared(&s->ram_lock);
            if (rubraview_filmbuf_has(&s->ramw, b)) {
                proven_sys_mem_copy(out, s->ram + rubraview_filmbuf_slot(&s->ramw, b) * BLOCK + off, n);
                from_ram = true;
            }
            ReleaseSRWLockShared(&s->ram_lock);
        }
        if (from_ram) {
            atomic_fetch_add(&g_from_memory, n);
        } else if (has_block(s, b) && pread_at(s->copy, pos, out, n, &took) && took == n) {
            atomic_fetch_add(&g_from_copy, n);
        } else if (!s->copy) {
            if (!pread_at(s->source, pos, out, n, &took) || took != n) { ok = false; break; }
            if (!s->local) atomic_fetch_add(&g_from_source, n);
        } else {
            if (!buf && !(buf = (uint8_t *)rubraview_alloc(BLOCK))) { ok = false; break; }
            DWORD whole = fetch_block(s, s->source, b, buf);
            if (whole < off + n) { ok = false; break; }
            proven_sys_mem_copy(out, buf + off, n);
        }
        atomic_store(&s->ahead_from, b + 1);
        atomic_store(&s->read_at, b);
        out += n; pos += n; left -= n;
        *got += n;
    }
    rubraview_free(buf);
    return ok || *got > 0;
}

/* ---- the limit ---- */

/* Whether a copy named `stem` is open in this process. Under g_list_lock. */
static bool stem_open(const WCHAR *stem) {
    for (rubraview_filmcache_stream_t *e = g_list; e; e = e->next) {
        WCHAR w[17];
        MultiByteToWideChar(CP_UTF8, 0, e->name, -1, w, 17);
        if (rubraview_wcmp_ci(w, stem) == 0) return true;
    }
    return false;
}

static uint64_t delete_copy(const WCHAR *folder, const WCHAR *stem) {
    WCHAR bin[WPATH], map[WPATH];
    rubraview_wfmt(bin, WPATH, "{}\\{}.bin", PROVEN_ARG(rubraview_wide_view(folder)), PROVEN_ARG(rubraview_wide_view(stem))); bin[WPATH - 1] = 0;
    rubraview_wfmt(map, WPATH, "{}\\{}.map", PROVEN_ARG(rubraview_wide_view(folder)), PROVEN_ARG(rubraview_wide_view(stem))); map[WPATH - 1] = 0;
    DWORD high = 0, low = GetCompressedFileSizeW(bin, &high);
    uint64_t bytes = low == INVALID_FILE_SIZE && GetLastError() != NO_ERROR ? 0 : ((uint64_t)high << 32) | low;
    if (!DeleteFileW(bin) && GetLastError() != ERROR_FILE_NOT_FOUND) return 0;   /* open elsewhere: kept */
    DeleteFileW(map);
    return bytes;
}

typedef struct copy_seen { WCHAR stem[17]; } copy_seen_t;

/* Copies go, least recently played first, till `incoming` more bytes fit; `all`: every one not open. */
static uint64_t make_room(const WCHAR *folder, uint64_t limit, uint64_t incoming, bool all) {
    WCHAR pattern[WPATH];
    rubraview_wfmt(pattern, WPATH, "{}\\*.bin", PROVEN_ARG(rubraview_wide_view(folder))); pattern[WPATH - 1] = 0;
    rubraview_filmcache_entry_t *entries = (rubraview_filmcache_entry_t *)rubraview_alloc_zero(MAX_COPIES, sizeof *entries);
    copy_seen_t *seen = (copy_seen_t *)rubraview_alloc_zero(MAX_COPIES, sizeof *seen);
    size_t *victims = (size_t *)rubraview_alloc_zero(MAX_COPIES, sizeof *victims);
    uint64_t freed = 0;
    if (!entries || !seen || !victims) goto done;
    size_t count = 0;
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileW(pattern, &fd);
    if (find == INVALID_HANDLE_VALUE) goto done;
    AcquireSRWLockShared(&g_list_lock);
    do {
        size_t len = rubraview_wlen(fd.cFileName);
        if (len != 20 || count >= MAX_COPIES) continue;   /* 16 hex digits and ".bin" */
        proven_sys_mem_copy(seen[count].stem, fd.cFileName, 16 * sizeof(WCHAR));   /* the 16 hex digits */
        seen[count].stem[16] = 0;
        WCHAR bin[WPATH], map[WPATH];
        rubraview_wfmt(bin, WPATH, "{}\\{}", PROVEN_ARG(rubraview_wide_view(folder)), PROVEN_ARG(rubraview_wide_view(fd.cFileName))); bin[WPATH - 1] = 0;
        rubraview_wfmt(map, WPATH, "{}\\{}.map", PROVEN_ARG(rubraview_wide_view(folder)), PROVEN_ARG(rubraview_wide_view(seen[count].stem))); map[WPATH - 1] = 0;
        DWORD high = 0, low = GetCompressedFileSizeW(bin, &high);
        WIN32_FILE_ATTRIBUTE_DATA md;
        FILETIME used = fd.ftLastWriteTime;
        if (GetFileAttributesExW(map, GetFileExInfoStandard, &md)) used = md.ftLastWriteTime;
        entries[count] = (rubraview_filmcache_entry_t){
            .bytes = low == INVALID_FILE_SIZE ? 0 : ((uint64_t)high << 32) | low,
            .used = (int64_t)(((uint64_t)used.dwHighDateTime << 32) | used.dwLowDateTime),
            .open = stem_open(seen[count].stem),
        };
        count++;
    } while (FindNextFileW(find, &fd));
    ReleaseSRWLockShared(&g_list_lock);
    FindClose(find);
    size_t k = rubraview_filmcache_evict(entries, count, all ? 0 : limit, all ? 1 : incoming, victims, MAX_COPIES);
    for (size_t i = 0; i < k; ++i) freed += delete_copy(folder, seen[victims[i]].stem);
done:
    rubraview_free(entries); rubraview_free(seen); rubraview_free(victims);
    return freed;
}

size_t rubraview_pal_filmcache_usage(uint64_t *bytes) {
    WCHAR folder[WPATH], pattern[WPATH];
    size_t count = 0;
    if (bytes) *bytes = 0;
    if (!folder_at(folder, WPATH, false)) return 0;
    rubraview_wfmt(pattern, WPATH, "{}\\*.bin", PROVEN_ARG(rubraview_wide_view(folder))); pattern[WPATH - 1] = 0;
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileW(pattern, &fd);
    if (find == INVALID_HANDLE_VALUE) return 0;
    do {
        if (rubraview_wlen(fd.cFileName) != 20) continue;
        WCHAR bin[WPATH];
        rubraview_wfmt(bin, WPATH, "{}\\{}", PROVEN_ARG(rubraview_wide_view(folder)), PROVEN_ARG(rubraview_wide_view(fd.cFileName))); bin[WPATH - 1] = 0;
        DWORD high = 0, low = GetCompressedFileSizeW(bin, &high);
        if (bytes && low != INVALID_FILE_SIZE) *bytes += ((uint64_t)high << 32) | low;
        count++;
    } while (FindNextFileW(find, &fd));
    FindClose(find);
    return count;
}

uint64_t rubraview_pal_filmcache_clear(void) {
    WCHAR folder[WPATH];
    return folder_at(folder, WPATH, false) ? make_room(folder, 0, 0, true) : 0;
}

void rubraview_pal_filmcache_forget(proven_u8str_view_t path) {
    WCHAR folder[WPATH], stem[17];
    char name[17];
    if (!folder_at(folder, WPATH, false)) return;
    rubraview_filmcache_name(name, path);
    MultiByteToWideChar(CP_UTF8, 0, name, -1, stem, 17);
    AcquireSRWLockShared(&g_list_lock);
    bool open = stem_open(stem);
    ReleaseSRWLockShared(&g_list_lock);
    if (!open) delete_copy(folder, stem);
}

/* ---- open and close ---- */

static void stream_free(rubraview_filmcache_stream_t *s) {
    say("close {}: {} of {} blocks here", PROVEN_ARG(s->path), PROVEN_ARG((unsigned long long)s->map.filled), PROVEN_ARG((unsigned long long)s->map.blocks));
    atomic_store(&s->closing, true);
    if (s->ahead_thread) { WaitForSingleObject(s->ahead_thread, INFINITE); CloseHandle(s->ahead_thread); }
    if (s->ram_thread) { WaitForSingleObject(s->ram_thread, INFINITE); CloseHandle(s->ram_thread); }
    if (s->ram_source) CloseHandle(s->ram_source);
    if (s->ram) {
        rubraview_pal_media_log("memory: let go, {} of {} blocks held", PROVEN_ARG((unsigned long long)s->ramw.held), PROVEN_ARG((unsigned long long)s->ramw.slots));
        rubraview_free(s->ram);
        atomic_store(&g_memory_held, 0);
    }
    rubraview_filmbuf_free(&s->ramw);
    if (s->copy) {
        FlushFileBuffers(s->copy);   /* the bytes on the disk before the map that names them */
        AcquireSRWLockExclusive(&s->lock);
        save_map(s);
        ReleaseSRWLockExclusive(&s->lock);
        CloseHandle(s->copy);
    }
    if (s->ahead_source) CloseHandle(s->ahead_source);
    if (s->source) CloseHandle(s->source);
    rubraview_filmcache_map_free(&s->map);
    rubraview_free(s);
}

void rubraview_pal_filmcache_close(rubraview_filmcache_stream_t *s) {
    if (!s) return;
    AcquireSRWLockExclusive(&g_list_lock);
    bool last = --s->refs == 0;
    if (last) {
        for (rubraview_filmcache_stream_t **p = &g_list; *p; p = &(*p)->next) if (*p == s) { *p = s->next; break; }
        if (g_last == s) g_last = NULL;
    }
    ReleaseSRWLockExclusive(&g_list_lock);
    if (last) stream_free(s);
}

static HANDLE open_source(const wchar_t *path) {
    return CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL, NULL);
}

/* The copy and its map, or the copy started again when its map is not this source's. */
static void open_copy(rubraview_filmcache_stream_t *s, uint64_t size, int64_t time) {
    s->copy = CreateFileW(s->bin, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (s->copy == INVALID_HANDLE_VALUE) { s->copy = NULL; return; }   /* another process writes it: read through */
    DWORD io = 0;
    DeviceIoControl(s->copy, FSCTL_SET_SPARSE, NULL, 0, NULL, 0, &io, NULL);   /* a block far in costs only itself */
    proven_u8str_view_t path = { .ptr = (const proven_byte_t *)s->path, .size = s->path_len };
    bool kept = false;
    HANDLE m = CreateFileW(s->map_file, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (m != INVALID_HANDLE_VALUE) {
        LARGE_INTEGER n;
        if (GetFileSizeEx(m, &n) && n.QuadPart > 0 && n.QuadPart < (64 << 20)) {
            char *text = (char *)rubraview_alloc((size_t)n.QuadPart);
            DWORD got = 0;
            if (text && ReadFile(m, text, (DWORD)n.QuadPart, &got, NULL) && got == (DWORD)n.QuadPart)
                kept = rubraview_filmcache_map_parse(&s->map, rubraview_bytes_view(text, got), path, size, time);
            rubraview_free(text);
        }
        CloseHandle(m);
    }
    LARGE_INTEGER have;
    if (kept && GetFileSizeEx(s->copy, &have)) {
        /* the copy reaches as far as its last filled block, or it was cut: started again */
        uint64_t last = s->map.blocks;
        while (last > 0 && !rubraview_filmcache_map_has(&s->map, last - 1)) --last;
        uint64_t need = last == 0 ? 0 : ((last - 1) * BLOCK + BLOCK < size ? (last - 1) * BLOCK + BLOCK : size);
        if ((uint64_t)have.QuadPart < need) { rubraview_filmcache_map_free(&s->map); kept = false; }
    }
    if (!kept) {
        LARGE_INTEGER zero = { 0 };
        SetFilePointerEx(s->copy, zero, NULL, FILE_BEGIN);
        SetEndOfFile(s->copy);
        if (!rubraview_filmcache_map_make(&s->map, size, time)) { CloseHandle(s->copy); s->copy = NULL; return; }
    }
    s->saved = s->map.filled;
    save_map(s);   /* when it was last played, for the limit */
}

/* ---- RV-261: the film in memory ---- */

static bool memory_now(uint64_t *total, uint64_t *avail) {
    MEMORYSTATUSEX ms = { .dwLength = sizeof ms };
    if (!GlobalMemoryStatusEx(&ms)) return false;
    *total = ms.ullTotalPhys;
    *avail = ms.ullAvailPhys;
    return true;
}

static DWORD WINAPI ram_main(void *arg) {
    rubraview_filmcache_stream_t *s = (rubraview_filmcache_stream_t *)arg;
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    uint32_t since_check = 0;
    bool held_back = false;
    while (!atomic_load(&s->closing)) {
        if (since_check++ % 16 == 0) {   /* 4 MB: the memory free looked at */
            uint64_t total = 0, avail = 0;
            AcquireSRWLockShared(&s->ram_lock);
            uint64_t held = s->ramw.held * (uint64_t)BLOCK;
            ReleaseSRWLockShared(&s->ram_lock);
            if (memory_now(&total, &avail) && !rubraview_filmbuf_may_fill(total, avail, held)) {
                if (!held_back) rubraview_pal_media_log("memory: held back at {} MB, the memory free is 20 % or less", PROVEN_ARG((unsigned long long)(held >> 20)));
                held_back = true;
                since_check = 0;
                Sleep(500);
                continue;
            }
            held_back = false;
        }
        AcquireSRWLockShared(&s->ram_lock);
        uint64_t b = rubraview_filmbuf_next(&s->ramw, atomic_load(&s->read_at), atomic_load(&s->ram_limit));
        uint64_t held = s->ramw.held, slots = s->ramw.slots;
        ReleaseSRWLockShared(&s->ram_lock);
        atomic_store(&g_memory_held, held * (uint64_t)BLOCK);
        if (b == UINT64_MAX) {
            if (held == slots && !atomic_exchange(&s->ram_full_said, true))
                rubraview_pal_media_log("memory: {} of the film held ({} MB)", PROVEN_ARG(s->ramw.whole ? "all" : "a window"),
                                        PROVEN_ARG((unsigned long long)((held * (uint64_t)BLOCK) >> 20)));
            Sleep(40);   /* nothing to read now: the reading moves on, or more is allowed */
            continue;
        }
        uint8_t *slot = s->ram + rubraview_filmbuf_slot(&s->ramw, b) * BLOCK;
        AcquireSRWLockExclusive(&s->ram_lock);
        rubraview_filmbuf_drop(&s->ramw, b);   /* the slot's block before is gone; no reader takes it now */
        ReleaseSRWLockExclusive(&s->ram_lock);
        uint64_t at = b * BLOCK;
        DWORD want = s->map.size - at < BLOCK ? (DWORD)(s->map.size - at) : BLOCK, got = 0;
        bool read = false;
        if (has_block(s, b)) read = pread_at(s->copy, at, slot, want, &got) && got == want;
        else if (s->copy) read = fetch_block(s, s->ram_source, b, slot) == want;
        else read = pread_at(s->ram_source, at, slot, want, &got) && got == want;
        if (!read) { Sleep(200); continue; }
        AcquireSRWLockExclusive(&s->ram_lock);
        rubraview_filmbuf_put(&s->ramw, b);
        ReleaseSRWLockExclusive(&s->ram_lock);
    }
    return 0;
}

/* Memory for the film, and its reading thread; nothing (the stream reads as before) when it is off or fails. */
static void ram_start(rubraview_filmcache_stream_t *s, const wchar_t *path, bool head_only) {
    uint64_t setting = atomic_load(&g_memory_setting), total = 0, avail = 0;
    if (setting == 0 || !memory_now(&total, &avail)) return;
    uint64_t budget = rubraview_filmbuf_budget(s->map.size, setting, total, avail);
    if (!rubraview_filmbuf_make(&s->ramw, s->map.size, budget)) return;
    s->ram = (uint8_t *)rubraview_alloc((size_t)(s->ramw.slots * (uint64_t)BLOCK));
    s->ram_source = s->ram ? open_source(path) : INVALID_HANDLE_VALUE;
    if (s->ram_source == INVALID_HANDLE_VALUE) { s->ram_source = NULL; rubraview_free(s->ram); s->ram = NULL; rubraview_filmbuf_free(&s->ramw); return; }
    InitializeSRWLock(&s->ram_lock);
    uint64_t head = rubraview_filmbuf_head_bytes(0.0);
    atomic_store(&s->ram_limit, head_only ? (head + BLOCK - 1) / BLOCK : UINT64_MAX);
    atomic_store(&g_memory_room, s->ramw.slots * (uint64_t)BLOCK);
    s->ram_thread = CreateThread(NULL, 0, ram_main, s, 0, NULL);
    rubraview_pal_media_log("memory: {} MB for a film of {} MB ({}), {}", PROVEN_ARG((unsigned long long)((s->ramw.slots * (uint64_t)BLOCK) >> 20)),
                            PROVEN_ARG((unsigned long long)(s->map.size >> 20)), PROVEN_ARG(s->ramw.whole ? "all of it" : "a window"),
                            PROVEN_ARG(head_only ? "its head only, opened ahead" : "read on to the end"));
}

void rubraview_pal_filmcache_read_limit(rubraview_filmcache_stream_t *s, uint64_t bytes) {
    if (!s || !s->ram) return;
    uint64_t blocks = bytes == UINT64_MAX ? UINT64_MAX : (bytes + BLOCK - 1) / BLOCK;
    if (atomic_exchange(&s->ram_limit, blocks) != blocks)
        rubraview_pal_media_log("memory: reads {}", PROVEN_ARG(bytes == UINT64_MAX ? "on to the end" : "the head"));
}

rubraview_filmcache_stream_t *rubraview_pal_filmcache_open(const wchar_t *path) {
    return rubraview_pal_filmcache_open_ex(path, false);
}

rubraview_filmcache_stream_t *rubraview_pal_filmcache_open_ex(const wchar_t *path, bool head_only) {
    uint64_t limit = 0;
    bool ahead = false;
    rubraview_pal_filmcache_settings(NULL, 0, &limit, &ahead);
    bool net = path && on_network(path);
    bool copies = net && limit > 0;
    bool memory = atomic_load(&g_memory_setting) > 0;
    if (!path || (!copies && !memory)) { say("not cached: {}", PROVEN_ARG(!path ? "no path" : limit == 0 ? "off" : "not a network drive")); return NULL; }

    rubraview_filmcache_stream_t *s = (rubraview_filmcache_stream_t *)rubraview_alloc_zero(1, sizeof *s);
    if (!s) return NULL;
    InitializeSRWLock(&s->lock);
    s->refs = 1;
    int n = WideCharToMultiByte(CP_UTF8, 0, path, -1, s->path, (int)sizeof s->path, NULL, NULL);
    if (n <= 1) { rubraview_free(s); return NULL; }
    s->path_len = (size_t)n - 1;
    proven_u8str_view_t p8 = { .ptr = (const proven_byte_t *)s->path, .size = s->path_len };
    rubraview_filmcache_name(s->name, p8);

    s->source = open_source(path);
    if (s->source == INVALID_HANDLE_VALUE) { rubraview_free(s); return NULL; }
    BY_HANDLE_FILE_INFORMATION info;
    if (!GetFileInformationByHandle(s->source, &info)) { CloseHandle(s->source); rubraview_free(s); return NULL; }
    uint64_t size = ((uint64_t)info.nFileSizeHigh << 32) | info.nFileSizeLow;
    int64_t time = (int64_t)(((uint64_t)info.ftLastWriteTime.dwHighDateTime << 32) | info.ftLastWriteTime.dwLowDateTime);
    if (size < RUBRAVIEW_FILMCACHE_FLOOR) { CloseHandle(s->source); rubraview_free(s); return NULL; }
    if (!copies) {   /* RV-261: memory alone: read straight from the file, no copy, not in the cache's list */
        s->local = !net;
        if (!rubraview_filmcache_map_make(&s->map, size, time)) { CloseHandle(s->source); rubraview_free(s); return NULL; }
        ram_start(s, path, head_only);
        if (!s->ram) { stream_free(s); return NULL; }   /* nothing to add: read the file as before */
        return s;
    }

    /* The same film open already in this process: one copy, read by both. */
    AcquireSRWLockExclusive(&g_list_lock);
    for (rubraview_filmcache_stream_t *e = g_list; e; e = e->next) {
        if (rubraview_cstr_cmp(e->name, s->name) != 0 || e->map.size != size || e->map.time != time) continue;
        e->refs++;
        g_last = e;
        say("open {} again: {} readers", PROVEN_ARG(e->path), PROVEN_ARG((long)e->refs));
        ReleaseSRWLockExclusive(&g_list_lock);
        CloseHandle(s->source);
        rubraview_free(s);
        return e;
    }
    ReleaseSRWLockExclusive(&g_list_lock);

    WCHAR folder[WPATH], stem[17];
    MultiByteToWideChar(CP_UTF8, 0, s->name, -1, stem, 17);
    if (folder_at(folder, WPATH, true)) {
        rubraview_wfmt(s->bin, WPATH, "{}\\{}.bin", PROVEN_ARG(rubraview_wide_view(folder)), PROVEN_ARG(rubraview_wide_view(stem))); s->bin[WPATH - 1] = 0;
        rubraview_wfmt(s->map_file, WPATH, "{}\\{}.map", PROVEN_ARG(rubraview_wide_view(folder)), PROVEN_ARG(rubraview_wide_view(stem))); s->map_file[WPATH - 1] = 0;
        make_room(folder, limit, size, false);
        if (size <= limit) open_copy(s, size, time);   /* a film larger than the whole cache is read through */
    }
    if (!s->copy && !rubraview_filmcache_map_make(&s->map, size, time)) { CloseHandle(s->source); rubraview_free(s); return NULL; }
    if (s->copy && ahead && s->map.filled < s->map.blocks) {
        s->ahead_source = open_source(path);
        if (s->ahead_source == INVALID_HANDLE_VALUE) s->ahead_source = NULL;
        else s->ahead_thread = CreateThread(NULL, 0, ahead_main, s, 0, NULL);
    }
    ram_start(s, path, head_only);   /* RV-261 */
    AcquireSRWLockExclusive(&g_list_lock);
    s->next = g_list;
    g_list = s;
    g_last = s;
    ReleaseSRWLockExclusive(&g_list_lock);
    say("open {}: {}, {} of {} blocks here, ahead {}", PROVEN_ARG(s->path), PROVEN_ARG(s->copy ? "writing the copy" : "read through"),
        PROVEN_ARG((unsigned long long)s->map.filled), PROVEN_ARG((unsigned long long)s->map.blocks), PROVEN_ARG(s->ahead_thread ? "on" : ahead ? "asked, not started" : "off"));
    return s;
}

/* ---- stats ---- */

void rubraview_pal_filmcache_own_stats(rubraview_filmcache_stats_t *out) {
    if (!out) return;
    *out = (rubraview_filmcache_stats_t){ .from_copy = atomic_load(&g_from_copy), .from_source = atomic_load(&g_from_source),
                                          .from_memory = atomic_load(&g_from_memory),
                                          .memory_held = atomic_load(&g_memory_held), .memory_room = atomic_load(&g_memory_held) ? atomic_load(&g_memory_room) : 0 };
    AcquireSRWLockShared(&g_list_lock);
    out->open = g_list != NULL;
    if (g_last) {
        AcquireSRWLockShared(&g_last->lock);
        out->filled = g_last->copy ? g_last->map.filled : 0;
        out->blocks = g_last->map.blocks;
        ReleaseSRWLockShared(&g_last->lock);
    }
    ReleaseSRWLockShared(&g_list_lock);
}

void rubraview_mhost_children_cache(rubraview_filmcache_stats_t *add);   /* pal_media_host.c */

void rubraview_pal_filmcache_stats(rubraview_filmcache_stats_t *out) {
    if (!out) return;
    rubraview_pal_filmcache_own_stats(out);
    rubraview_mhost_children_cache(out);
}

/* ---- an IStream for Media Foundation ---- */

typedef struct cache_istream {
    IStream face;
    LONG refs;
    rubraview_filmcache_stream_t *s;
    uint64_t at;
} cache_istream_t;

static HRESULT STDMETHODCALLTYPE is_query(IStream *self, REFIID riid, void **out) {
    if (!out) return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ISequentialStream) || IsEqualIID(riid, &IID_IStream)) {
        *out = self;
        IStream_AddRef(self);
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE is_addref(IStream *self) { return (ULONG)InterlockedIncrement(&((cache_istream_t *)self)->refs); }
static ULONG STDMETHODCALLTYPE is_release(IStream *self) {
    cache_istream_t *c = (cache_istream_t *)self;
    LONG left = InterlockedDecrement(&c->refs);
    if (left == 0) { rubraview_pal_filmcache_close(c->s); rubraview_free(c); }
    return (ULONG)left;
}
static HRESULT STDMETHODCALLTYPE is_read(IStream *self, void *dst, ULONG len, ULONG *read) {
    cache_istream_t *c = (cache_istream_t *)self;
    uint32_t got = 0;
    bool ok = rubraview_pal_filmcache_read(c->s, c->at, rubraview_mem_mut(dst, len), &got);
    c->at += got;
    if (read) *read = got;
    if (!ok) return STG_E_READFAULT;
    return got < len ? S_FALSE : S_OK;
}
static HRESULT STDMETHODCALLTYPE is_write(IStream *self, const void *src, ULONG len, ULONG *written) {
    (void)self; (void)src; (void)len;
    if (written) *written = 0;
    return STG_E_ACCESSDENIED;
}
static HRESULT STDMETHODCALLTYPE is_seek(IStream *self, LARGE_INTEGER move, DWORD origin, ULARGE_INTEGER *now) {
    cache_istream_t *c = (cache_istream_t *)self;
    int64_t base = origin == STREAM_SEEK_SET ? 0 : origin == STREAM_SEEK_CUR ? (int64_t)c->at
                 : origin == STREAM_SEEK_END ? (int64_t)rubraview_pal_filmcache_size(c->s) : -1;
    if (base < 0 && origin != STREAM_SEEK_SET) return STG_E_INVALIDFUNCTION;
    int64_t to = base + move.QuadPart;
    if (to < 0) return STG_E_INVALIDFUNCTION;
    c->at = (uint64_t)to;
    if (now) now->QuadPart = c->at;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE is_setsize(IStream *self, ULARGE_INTEGER size) { (void)self; (void)size; return STG_E_ACCESSDENIED; }
static HRESULT STDMETHODCALLTYPE is_copyto(IStream *self, IStream *to, ULARGE_INTEGER n, ULARGE_INTEGER *r, ULARGE_INTEGER *w) {
    (void)self; (void)to; (void)n; (void)r; (void)w; return E_NOTIMPL;
}
static HRESULT STDMETHODCALLTYPE is_commit(IStream *self, DWORD flags) { (void)self; (void)flags; return S_OK; }
static HRESULT STDMETHODCALLTYPE is_revert(IStream *self) { (void)self; return E_NOTIMPL; }
static HRESULT STDMETHODCALLTYPE is_lock(IStream *self, ULARGE_INTEGER at, ULARGE_INTEGER n, DWORD kind) {
    (void)self; (void)at; (void)n; (void)kind; return STG_E_INVALIDFUNCTION;
}
static HRESULT STDMETHODCALLTYPE is_stat(IStream *self, STATSTG *st, DWORD flags) {
    cache_istream_t *c = (cache_istream_t *)self;
    if (!st) return STG_E_INVALIDPOINTER;
    proven_sys_mem_set(st, 0, sizeof *st);
    st->type = STGTY_STREAM;
    st->cbSize.QuadPart = rubraview_pal_filmcache_size(c->s);
    st->grfMode = STGM_READ;
    if (!(flags & STATFLAG_NONAME)) {
        /* the file's name, whose ending tells Media Foundation the kind */
        int n = MultiByteToWideChar(CP_UTF8, 0, c->s->path, -1, NULL, 0);
        st->pwcsName = n > 0 ? (LPOLESTR)CoTaskMemAlloc((size_t)n * sizeof(WCHAR)) : NULL;
        if (st->pwcsName) MultiByteToWideChar(CP_UTF8, 0, c->s->path, -1, st->pwcsName, n);
    }
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE is_clone(IStream *self, IStream **out) {
    cache_istream_t *c = (cache_istream_t *)self;
    if (!out) return E_POINTER;
    *out = (IStream *)rubraview_pal_filmcache_istream(c->s);
    if (!*out) return E_OUTOFMEMORY;
    ((cache_istream_t *)*out)->at = c->at;
    return S_OK;
}

static IStreamVtbl g_istream_vtbl = {
    is_query, is_addref, is_release, is_read, is_write, is_seek, is_setsize, is_copyto,
    is_commit, is_revert, is_lock, is_lock, is_stat, is_clone,
};

void *rubraview_pal_filmcache_istream(rubraview_filmcache_stream_t *s) {
    if (!s) return NULL;
    cache_istream_t *c = (cache_istream_t *)rubraview_alloc_zero(1, sizeof *c);
    if (!c) return NULL;
    c->face.lpVtbl = &g_istream_vtbl;
    c->refs = 1;
    AcquireSRWLockExclusive(&g_list_lock);
    s->refs++;
    ReleaseSRWLockExclusive(&g_list_lock);
    c->s = s;
    return c;
}

#endif /* _WIN32 */
