#include "rubraview/path.h"
#include <string.h>
#include <ctype.h>

u8str_t rv_path_dirname(u8str_t path) {
    if (path.ptr == NULL || path.len == 0) {
        return (u8str_t){ .ptr = "", .len = 0 };
    }

    /* Scan backwards for last separator */
    for (proven_size_t i = path.len; i > 0; --i) {
        proven_size_t idx = i - 1;
        if (rv_path_is_sep(path.ptr[idx])) {
            /* If separator is at root (e.g. "/foo" -> "/"), preserve root */
            if (idx == 0) {
                return (u8str_t){ .ptr = path.ptr, .len = 1 };
            }
            return (u8str_t){ .ptr = path.ptr, .len = idx };
        }
    }

    return (u8str_t){ .ptr = path.ptr, .len = 0 };
}

u8str_t rv_path_basename(u8str_t path) {
    if (path.ptr == NULL || path.len == 0) {
        return (u8str_t){ .ptr = "", .len = 0 };
    }

    for (proven_size_t i = path.len; i > 0; --i) {
        proven_size_t idx = i - 1;
        if (rv_path_is_sep(path.ptr[idx])) {
            return (u8str_t){
                .ptr = path.ptr + idx + 1,
                .len = path.len - idx - 1
            };
        }
    }

    return path;
}

u8str_t rv_path_ext(u8str_t path) {
    u8str_t base = rv_path_basename(path);
    if (base.len == 0) return (u8str_t){ .ptr = "", .len = 0 };

    for (proven_size_t i = base.len; i > 0; --i) {
        proven_size_t idx = i - 1;
        if (base.ptr[idx] == '.') {
            /* Do not treat leading dot in hidden file (e.g. ".gitignore") as extension */
            if (idx == 0) {
                return (u8str_t){ .ptr = "", .len = 0 };
            }
            return (u8str_t){
                .ptr = base.ptr + idx,
                .len = base.len - idx
            };
        }
    }

    return (u8str_t){ .ptr = "", .len = 0 };
}

u8str_t rv_path_stem(u8str_t path) {
    u8str_t base = rv_path_basename(path);
    u8str_t ext = rv_path_ext(path);
    if (ext.len > 0) {
        return (u8str_t){
            .ptr = base.ptr,
            .len = base.len - ext.len
        };
    }
    return base;
}

bool rv_path_has_ext(u8str_t path, const char *ext) {
    if (!ext) return false;
    u8str_t pe = rv_path_ext(path);
    if (pe.len == 0) return false;

    /* If query ext starts with '.', match directly; if not, skip leading '.' of pe */
    const char *p_comp = pe.ptr;
    proven_size_t p_len = pe.len;
    if (ext[0] != '.' && p_comp[0] == '.') {
        p_comp++;
        p_len--;
    }

    size_t ext_len = strlen(ext);
    if (p_len != ext_len) return false;

    for (size_t i = 0; i < ext_len; ++i) {
        if (tolower((unsigned char)p_comp[i]) != tolower((unsigned char)ext[i])) {
            return false;
        }
    }
    return true;
}

u8str_t rv_path_join(proven_arena_t *arena, u8str_t dir, u8str_t filename) {
    if (!arena) return (u8str_t){ .ptr = "", .len = 0 };
    if (dir.len == 0) return filename;
    if (filename.len == 0) return dir;

    bool needs_sep = !rv_path_is_sep(dir.ptr[dir.len - 1]) && !rv_path_is_sep(filename.ptr[0]);
    proven_size_t total_len = dir.len + (needs_sep ? 1 : 0) + filename.len;

    /* Allocate total_len + 1 to enforce the null-terminated allocation invariant */
    proven_result_mem_mut_t res = proven_arena_alloc(arena, total_len + 1);
    if (!proven_is_ok(res.err)) {
        return (u8str_t){ .ptr = "", .len = 0 };
    }

    char *dst = (char*)res.value.ptr;
    memcpy(dst, dir.ptr, dir.len);
    proven_size_t offset = dir.len;
    if (needs_sep) {
        dst[offset++] = '/';
    }
    memcpy(dst + offset, filename.ptr, filename.len);
    offset += filename.len;
    dst[offset] = '\0'; /* Guaranteed null terminator */

    return (u8str_t){
        .ptr = dst,
        .len = total_len
    };
}
