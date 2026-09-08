#ifdef _WIN32
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "rubraview/pal/pal_fs.h"
#include "rubraview/path.h"

/* Windows FILETIME counts 100-nanosecond intervals since 1601-01-01;
   the Unix epoch is 11644473600 seconds later. */
#define FILETIME_UNIX_EPOCH_DIFF 116444736000000000LL

static int64_t filetime_to_unix_seconds(FILETIME ft) {
    ULARGE_INTEGER v;
    v.LowPart = ft.dwLowDateTime;
    v.HighPart = ft.dwHighDateTime;
    if (v.QuadPart == 0) return 0;
    return (int64_t)((v.QuadPart - (ULONGLONG)FILETIME_UNIX_EPOCH_DIFF) / 10000000ULL);
}

static u8str_t arena_dup(proven_arena_t *arena, const char *src, size_t n) {
    proven_result_mem_mut_t res = proven_arena_alloc(arena, n + 1);
    if (!proven_is_ok(res.err)) return (u8str_t){ .ptr = "", .len = 0 };
    if (n > 0) memcpy(res.value.ptr, src, n);
    res.value.ptr[n] = '\0';
    return (u8str_t){ .ptr = (const char*)res.value.ptr, .len = n };
}

/* UTF-8 to UTF-16 at the OS call boundary only (§7.2.3). Returns NULL on
   failure; the buffer is arena-allocated. */
static WCHAR *utf8_to_wide(proven_arena_t *arena, u8str_t utf8) {
    if (utf8.len > (size_t)INT32_MAX) return NULL;
    int wide_len = MultiByteToWideChar(CP_UTF8, 0, utf8.ptr, (int)utf8.len, NULL, 0);
    if (wide_len < 0) return NULL;

    proven_result_mem_mut_t res = proven_arena_alloc(arena, ((size_t)wide_len + 1) * sizeof(WCHAR));
    if (!proven_is_ok(res.err)) return NULL;

    WCHAR *wide = (WCHAR*)(void*)res.value.ptr;
    if (wide_len > 0) {
        MultiByteToWideChar(CP_UTF8, 0, utf8.ptr, (int)utf8.len, wide, wide_len);
    }
    wide[wide_len] = L'\0';
    return wide;
}

static u8str_t wide_to_u8str(proven_arena_t *arena, const WCHAR *wide) {
    if (!wide) return (u8str_t){ .ptr = "", .len = 0 };
    int utf8_len = WideCharToMultiByte(CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL);
    if (utf8_len <= 0) return (u8str_t){ .ptr = "", .len = 0 };

    proven_result_mem_mut_t res = proven_arena_alloc(arena, (size_t)utf8_len);
    if (!proven_is_ok(res.err)) return (u8str_t){ .ptr = "", .len = 0 };

    WideCharToMultiByte(CP_UTF8, 0, wide, -1, (char*)res.value.ptr, utf8_len, NULL, NULL);
    return (u8str_t){ .ptr = (const char*)res.value.ptr, .len = (size_t)(utf8_len - 1) };
}

typedef struct entry_buf {
    rubraview_fs_entry_t *data;
    size_t count, capacity;
} entry_buf_t;

static bool entry_buf_push(proven_arena_t *arena, entry_buf_t *b, rubraview_fs_entry_t entry) {
    if (b->count >= b->capacity) {
        size_t new_cap = b->capacity == 0 ? 32 : b->capacity * 2;
        proven_result_mem_mut_t res = proven_arena_alloc(arena, new_cap * sizeof(rubraview_fs_entry_t));
        if (!proven_is_ok(res.err)) return false;
        rubraview_fs_entry_t *new_data = (rubraview_fs_entry_t*)(void*)res.value.ptr;
        if (b->data && b->count > 0) memcpy(new_data, b->data, b->count * sizeof(rubraview_fs_entry_t));
        b->data = new_data;
        b->capacity = new_cap;
    }
    b->data[b->count++] = entry;
    return true;
}

rubraview_fs_listing_t rubraview_pal_fs_list_dir(proven_arena_t *arena, u8str_t dir_path) {
    rubraview_fs_listing_t listing = {0};
    if (!arena || dir_path.len == 0 || !dir_path.ptr) return listing;

    u8str_t dir_z = arena_dup(arena, dir_path.ptr, dir_path.len);
    if (dir_z.len == 0) return listing;

    /* FindFirstFileW needs a wildcard pattern: "<dir>\*". */
    u8str_t pattern = rubraview_path_join(arena, dir_z, (u8str_t){ .ptr = "*", .len = 1 });
    WCHAR *wide_pattern = utf8_to_wide(arena, pattern);
    if (!wide_pattern) return listing;

    WIN32_FIND_DATAW find_data;
    HANDLE find = FindFirstFileW(wide_pattern, &find_data);
    if (find == INVALID_HANDLE_VALUE) return listing;

    entry_buf_t buf = {0};
    do {
        if (wcscmp(find_data.cFileName, L".") == 0 || wcscmp(find_data.cFileName, L"..") == 0) continue;

        rubraview_fs_entry_t entry = {0};
        entry.name = wide_to_u8str(arena, find_data.cFileName);
        entry.path = rubraview_path_join(arena, dir_z, entry.name);

        ULARGE_INTEGER size;
        size.LowPart = find_data.nFileSizeLow;
        size.HighPart = find_data.nFileSizeHigh;
        entry.size_bytes = (uint64_t)size.QuadPart;
        entry.mtime = filetime_to_unix_seconds(find_data.ftLastWriteTime);
        entry.ctime = filetime_to_unix_seconds(find_data.ftCreationTime);
        entry.is_directory = (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

        entry_buf_push(arena, &buf, entry);
    } while (FindNextFileW(find, &find_data));

    FindClose(find);

    listing.entries = buf.data;
    listing.count = buf.count;
    return listing;
}

bool rubraview_pal_fs_stat(proven_arena_t *arena, u8str_t path, rubraview_fs_entry_t *out_entry) {
    if (!arena || !out_entry || path.len == 0) return false;

    u8str_t path_z = arena_dup(arena, path.ptr, path.len);
    if (path_z.len == 0) return false;

    WCHAR *wide_path = utf8_to_wide(arena, path_z);
    if (!wide_path) return false;

    WIN32_FILE_ATTRIBUTE_DATA attrs;
    if (!GetFileAttributesExW(wide_path, GetFileExInfoStandard, &attrs)) return false;

    ULARGE_INTEGER size;
    size.LowPart = attrs.nFileSizeLow;
    size.HighPart = attrs.nFileSizeHigh;

    *out_entry = (rubraview_fs_entry_t){0};
    out_entry->path = path_z;
    out_entry->name = rubraview_path_basename(path_z);
    out_entry->size_bytes = (uint64_t)size.QuadPart;
    out_entry->mtime = filetime_to_unix_seconds(attrs.ftLastWriteTime);
    out_entry->ctime = filetime_to_unix_seconds(attrs.ftCreationTime);
    out_entry->is_directory = (attrs.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    return true;
}

bool rubraview_pal_fs_exists(u8str_t path) {
    if (path.len == 0 || !path.ptr || path.len >= MAX_PATH * 4) return false;

    /* The caller's slice need not be NUL-terminated, so convert from a
       bounded stack copy rather than reading past path.len. */
    char narrow[MAX_PATH * 4];
    memcpy(narrow, path.ptr, path.len);
    narrow[path.len] = '\0';

    WCHAR wide[MAX_PATH * 2];
    int wide_len = MultiByteToWideChar(CP_UTF8, 0, narrow, -1, wide, (int)(sizeof(wide) / sizeof(wide[0])));
    if (wide_len <= 0) return false;

    return GetFileAttributesW(wide) != INVALID_FILE_ATTRIBUTES;
}

u8str_t rubraview_pal_fs_read_file(proven_arena_t *arena, u8str_t path, size_t max_bytes) {
    u8str_t empty = { .ptr = "", .len = 0 };
    if (!arena || path.len == 0 || !path.ptr) return empty;

    u8str_t path_z = arena_dup(arena, path.ptr, path.len);
    if (path_z.len == 0) return empty;

    FILE *file = fopen(path_z.ptr, "rb");
    if (!file) return empty;

    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return empty; }
    long size = ftell(file);
    if (size < 0 || (size_t)size > max_bytes) { fclose(file); return empty; }
    rewind(file);

    proven_result_mem_mut_t res = proven_arena_alloc(arena, (size_t)size + 1);
    if (!proven_is_ok(res.err)) { fclose(file); return empty; }

    size_t read = fread(res.value.ptr, 1, (size_t)size, file);
    fclose(file);

    res.value.ptr[read] = '\0';
    return (u8str_t){ .ptr = (const char*)res.value.ptr, .len = read };
}

#endif /* _WIN32 */
