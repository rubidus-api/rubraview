#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <sys/stat.h>
#include <string.h>
#include "rubraview/pal/pal_fs.h"
#include "rubraview/path.h"

/* Copies `n` bytes into the arena as len + 1 with a trailing NUL, per the
   null-terminated allocation invariant (§7.2.3). */
static u8str_t arena_dup(proven_arena_t *arena, const char *src, size_t n) {
    proven_result_mem_mut_t res = proven_arena_alloc(arena, n + 1);
    if (!proven_is_ok(res.err)) return (u8str_t){ .ptr = "", .len = 0 };
    if (n > 0) memcpy(res.value.ptr, src, n);
    res.value.ptr[n] = '\0';
    return (u8str_t){ .ptr = (const char*)res.value.ptr, .len = n };
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

static void fill_from_stat(rubraview_fs_entry_t *entry, const struct stat *st) {
    entry->size_bytes = (uint64_t)st->st_size;
    entry->mtime = (int64_t)st->st_mtime;
    entry->ctime = (int64_t)st->st_ctime; /* POSIX status-change time: the closest available to Windows creation time */
    entry->is_directory = S_ISDIR(st->st_mode);
}

rubraview_fs_listing_t rubraview_pal_fs_list_dir(proven_arena_t *arena, u8str_t dir_path) {
    rubraview_fs_listing_t listing = {0};
    if (!arena || dir_path.len == 0 || !dir_path.ptr) return listing;

    /* dir_path may be a slice; make a NUL-terminated copy for opendir. */
    u8str_t dir_z = arena_dup(arena, dir_path.ptr, dir_path.len);
    if (dir_z.len == 0) return listing;

    DIR *dir = opendir(dir_z.ptr);
    if (!dir) return listing;

    entry_buf_t buf = {0};
    struct dirent *de;
    while ((de = readdir(dir)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;

        size_t name_len = strlen(de->d_name);
        rubraview_fs_entry_t entry = {0};
        entry.name = arena_dup(arena, de->d_name, name_len);
        entry.path = rubraview_path_join(arena, dir_z, entry.name);

        struct stat st;
        if (entry.path.len > 0 && stat(entry.path.ptr, &st) == 0) {
            fill_from_stat(&entry, &st);
        }

        entry_buf_push(arena, &buf, entry);
    }
    closedir(dir);

    listing.entries = buf.data;
    listing.count = buf.count;
    return listing;
}

bool rubraview_pal_fs_stat(proven_arena_t *arena, u8str_t path, rubraview_fs_entry_t *out_entry) {
    if (!arena || !out_entry || path.len == 0) return false;

    u8str_t path_z = arena_dup(arena, path.ptr, path.len);
    if (path_z.len == 0) return false;

    struct stat st;
    if (stat(path_z.ptr, &st) != 0) return false;

    *out_entry = (rubraview_fs_entry_t){0};
    out_entry->path = path_z;
    out_entry->name = rubraview_path_basename(path_z);
    fill_from_stat(out_entry, &st);
    return true;
}

bool rubraview_pal_fs_exists(u8str_t path) {
    if (path.len == 0 || !path.ptr) return false;

    /* The caller's slice need not be NUL-terminated (it may point into a
       larger buffer), so copy into a bounded stack buffer rather than
       reading past path.len. */
    char buf[4096];
    if (path.len >= sizeof(buf)) return false;
    memcpy(buf, path.ptr, path.len);
    buf[path.len] = '\0';

    struct stat st;
    return stat(buf, &st) == 0;
}

#endif /* !_WIN32 */
