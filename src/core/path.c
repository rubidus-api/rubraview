#include "rubraview/path.h"
#include <string.h>
#include <ctype.h>

u8str_t rubraview_path_dirname(u8str_t path) {
    if (path.ptr == NULL || path.len == 0) {
        return (u8str_t){ .ptr = "", .len = 0 };
    }

    /* Scan backwards for last separator */
    for (proven_size_t i = path.len; i > 0; --i) {
        proven_size_t idx = i - 1;
        if (rubraview_path_is_sep(path.ptr[idx])) {
            /* If separator is at root (e.g. "/foo" -> "/"), preserve root */
            if (idx == 0) {
                return (u8str_t){ .ptr = path.ptr, .len = 1 };
            }
            return (u8str_t){ .ptr = path.ptr, .len = idx };
        }
    }

    return (u8str_t){ .ptr = path.ptr, .len = 0 };
}

u8str_t rubraview_path_basename(u8str_t path) {
    if (path.ptr == NULL || path.len == 0) {
        return (u8str_t){ .ptr = "", .len = 0 };
    }

    for (proven_size_t i = path.len; i > 0; --i) {
        proven_size_t idx = i - 1;
        if (rubraview_path_is_sep(path.ptr[idx])) {
            return (u8str_t){
                .ptr = path.ptr + idx + 1,
                .len = path.len - idx - 1
            };
        }
    }

    return path;
}

u8str_t rubraview_path_ext(u8str_t path) {
    u8str_t base = rubraview_path_basename(path);
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

u8str_t rubraview_path_stem(u8str_t path) {
    u8str_t base = rubraview_path_basename(path);
    u8str_t ext = rubraview_path_ext(path);
    if (ext.len > 0) {
        return (u8str_t){
            .ptr = base.ptr,
            .len = base.len - ext.len
        };
    }
    return base;
}

bool rubraview_path_has_ext(u8str_t path, const char *ext) {
    if (!ext) return false;
    u8str_t pe = rubraview_path_ext(path);
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

u8str_t rubraview_path_with_ext(proven_arena_t *arena, u8str_t path, const char *new_ext) {
    u8str_t empty = { .ptr = "", .len = 0 };
    if (!arena || path.len == 0 || !new_ext) return empty;

    u8str_t ext = rubraview_path_ext(path);
    proven_size_t keep = path.len - ext.len;          /* the path without its extension */
    proven_size_t add = (proven_size_t)strlen(new_ext);

    proven_result_mem_mut_t res = proven_arena_alloc(arena, keep + add + 1);
    if (!proven_is_ok(res.err)) return empty;
    char *dst = (char*)res.value.ptr;
    memcpy(dst, path.ptr, keep);
    memcpy(dst + keep, new_ext, add);
    dst[keep + add] = '\0';
    return (u8str_t){ .ptr = dst, .len = keep + add };
}

u8str_t rubraview_path_join(proven_arena_t *arena, u8str_t dir, u8str_t filename) {
    if (!arena) return (u8str_t){ .ptr = "", .len = 0 };
    if (dir.len == 0) return filename;
    if (filename.len == 0) return dir;

    bool needs_sep = !rubraview_path_is_sep(dir.ptr[dir.len - 1]) && !rubraview_path_is_sep(filename.ptr[0]);
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

bool rubraview_path_same(u8str_t a, u8str_t b) {
    if (a.len != b.len) return false;
    for (proven_size_t i = 0; i < a.len; ++i) {
        char x = a.ptr[i], y = b.ptr[i];
        if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
        if (rubraview_path_is_sep(x)) x = '/';
        if (rubraview_path_is_sep(y)) y = '/';
        if (x != y) return false;
    }
    return true;
}

static bool reserved_device_name(const char *name, size_t len) {
    /* The part before the first dot is what Windows compares. */
    size_t stem = 0;
    while (stem < len && name[stem] != '.') stem++;
    static const char *const NAMES[] = { "CON", "PRN", "AUX", "NUL" };
    char up[5] = {0};
    if (stem == 3 || stem == 4) {
        for (size_t i = 0; i < stem; ++i) {
            char c = name[i];
            up[i] = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
        }
        if (stem == 3) {
            for (size_t i = 0; i < 4; ++i) if (memcmp(up, NAMES[i], 3) == 0) return true;
        } else if ((memcmp(up, "COM", 3) == 0 || memcmp(up, "LPT", 3) == 0) && up[3] >= '1' && up[3] <= '9') {
            return true;
        }
    }
    return false;
}

u8str_t rubraview_path_safe_name(char *buf, size_t cap, u8str_t name) {
    u8str_t none = { .ptr = "", .len = 0 };
    if (!buf || cap < 16) return none;
    u8str_t base = rubraview_path_basename(name);

    /* Characters Windows refuses, and control bytes. */
    char clean[512];
    size_t n = 0;
    for (size_t i = 0; i < base.len && n < sizeof(clean); ++i) {
        unsigned char c = (unsigned char)base.ptr[i];
        bool bad = c < 32 || c == 127 || c == '<' || c == '>' || c == ':' || c == '"' ||
                   c == '/' || c == '\\' || c == '|' || c == '?' || c == '*';
        clean[n++] = bad ? '_' : (char)c;
    }
    while (n > 0 && (clean[n - 1] == '.' || clean[n - 1] == ' ')) n--;
    size_t lead = 0;
    while (lead < n && clean[lead] == ' ') lead++;

    size_t at = 0;
    if (n - lead == 0) {
        memcpy(buf, "dropped", 7);
        at = 7;
    } else {
        if (reserved_device_name(clean + lead, n - lead)) buf[at++] = '_';
        size_t room = cap - 1 - at;
        size_t len = n - lead;
        const char *src = clean + lead;
        if (len <= room) {
            memcpy(buf + at, src, len);
            at += len;
        } else {
            /* Too long: keep the extension (up to 16 bytes) and cut the stem,
               never inside a UTF-8 sequence. */
            size_t ext = 0;
            for (size_t i = len; i > 0 && len - i < 16; --i) {
                if (src[i - 1] == '.') { ext = len - (i - 1); break; }
            }
            if (ext >= room) ext = 0;
            size_t stem = room - ext;
            while (stem > 0 && ((unsigned char)src[stem] & 0xC0) == 0x80) stem--;
            memcpy(buf + at, src, stem);
            at += stem;
            memcpy(buf + at, src + len - ext, ext);
            at += ext;
        }
    }
    buf[at] = '\0';
    return (u8str_t){ .ptr = buf, .len = at };
}
