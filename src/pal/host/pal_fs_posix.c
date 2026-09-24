#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <sys/stat.h>
#include <stdio.h>
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
        proven_result_mem_mut_t res = rubraview_arena_alloc_array(arena, new_cap, sizeof(rubraview_fs_entry_t));
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

u8str_t rubraview_pal_transcode_codepage(proven_arena_t *arena, u8str_t bytes, uint32_t codepage_id) {
    (void)arena; (void)bytes; (void)codepage_id;
    /* The host build carries no legacy code-page tables — that is a
       Windows facility (§3.8.3 step 3) and the reason this call sits
       behind the PAL. Returning empty tells the caller to keep the raw
       bytes rather than display a wrong transliteration. */
    return (u8str_t){ .ptr = "", .len = 0 };
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

bool rubraview_pal_fs_write_file(u8str_t path, u8str_t contents) {
    if (path.len == 0 || !path.ptr || path.len >= 4096) return false;

    char path_z[4096];
    memcpy(path_z, path.ptr, path.len);
    path_z[path.len] = '\0';

    /* The folder the file goes in is made first: on a first run the
       settings folder does not exist yet (0.0.6: "could not write
       settings.ini" on every change). */
    for (size_t i = 1; i < path.len; ++i) {
        if (path_z[i] != '/') continue;
        path_z[i] = '\0';
        mkdir(path_z, 0777);   /* an existing level is fine; a real failure shows at fopen */
        path_z[i] = '/';
    }

    FILE *file = fopen(path_z, "wb");
    if (!file) return false;

    size_t written = contents.len > 0 ? fwrite(contents.ptr, 1, contents.len, file) : 0;
    bool ok = (written == contents.len);
    if (fclose(file) != 0) ok = false;
    return ok;
}

#endif /* !_WIN32 */

/* ---- file management (§3.18) ----
 *
 * The host build exists to run the tests, and no test writes to the real
 * filesystem: the decisions these calls sit behind are checked in
 * `tests/test_filemanage.c` against the model, not against a disk. So
 * these report failure rather than doing something a test did not ask
 * for. The Windows backend is the one that acts.
 */

bool rubraview_pal_fs_recycle(u8str_t path) { (void)path; return false; }
bool rubraview_pal_fs_delete(u8str_t path) { (void)path; return false; }
bool rubraview_pal_fs_restore_last_recycled(u8str_t original_path) { (void)original_path; return false; }
bool rubraview_pal_fs_move(u8str_t from, u8str_t to) { (void)from; (void)to; return false; }
bool rubraview_pal_fs_copy(u8str_t from, u8str_t to) { (void)from; (void)to; return false; }
bool rubraview_pal_fs_make_dirs(u8str_t path) { (void)path; return false; }
