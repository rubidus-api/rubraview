#include "rubraview/path.h"
#include "rubraview/number.h"
#include <string.h>
#include <ctype.h>

proven_u8str_view_t rubraview_path_dirname(proven_u8str_view_t path) {
    if (path.ptr == NULL || path.size == 0) {
        return PROVEN_LIT("");
    }

    /* Scan backwards for last separator */
    for (proven_size_t i = path.size; i > 0; --i) {
        proven_size_t idx = i - 1;
        if (rubraview_path_is_sep(path.ptr[idx])) {
            /* If separator is at root (e.g. "/foo" -> "/"), preserve root */
            if (idx == 0) {
                return (proven_u8str_view_t){ .ptr = path.ptr, .size = 1 };
            }
            return (proven_u8str_view_t){ .ptr = path.ptr, .size = idx };
        }
    }

    return (proven_u8str_view_t){ .ptr = path.ptr, .size = 0 };
}

proven_u8str_view_t rubraview_path_basename(proven_u8str_view_t path) {
    if (path.ptr == NULL || path.size == 0) {
        return PROVEN_LIT("");
    }

    for (proven_size_t i = path.size; i > 0; --i) {
        proven_size_t idx = i - 1;
        if (rubraview_path_is_sep(path.ptr[idx])) {
            return (proven_u8str_view_t){
                .ptr = path.ptr + idx + 1,
                .size = path.size - idx - 1
            };
        }
    }

    return path;
}

proven_u8str_view_t rubraview_path_ext(proven_u8str_view_t path) {
    proven_u8str_view_t base = rubraview_path_basename(path);
    if (base.size == 0) return PROVEN_LIT("");

    for (proven_size_t i = base.size; i > 0; --i) {
        proven_size_t idx = i - 1;
        if (base.ptr[idx] == '.') {
            /* Do not treat leading dot in hidden file (e.g. ".gitignore") as extension */
            if (idx == 0) {
                return PROVEN_LIT("");
            }
            return (proven_u8str_view_t){
                .ptr = base.ptr + idx,
                .size = base.size - idx
            };
        }
    }

    return PROVEN_LIT("");
}

proven_u8str_view_t rubraview_path_stem(proven_u8str_view_t path) {
    proven_u8str_view_t base = rubraview_path_basename(path);
    proven_u8str_view_t ext = rubraview_path_ext(path);
    if (ext.size > 0) {
        return (proven_u8str_view_t){
            .ptr = base.ptr,
            .size = base.size - ext.size
        };
    }
    return base;
}

bool rubraview_path_has_ext(proven_u8str_view_t path, const char *ext) {
    if (!ext) return false;
    proven_u8str_view_t pe = rubraview_path_ext(path);
    if (pe.size == 0) return false;

    /* If query ext starts with '.', match directly; if not, skip leading '.' of pe */
    const char *p_comp = (const char *)pe.ptr;
    proven_size_t p_len = pe.size;
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

proven_u8str_view_t rubraview_path_with_ext(proven_arena_t *arena, proven_u8str_view_t path, const char *new_ext) {
    proven_u8str_view_t empty = PROVEN_LIT("");
    if (!arena || path.size == 0 || !new_ext) return empty;

    proven_u8str_view_t ext = rubraview_path_ext(path);
    proven_size_t keep = path.size - ext.size;          /* the path without its extension */
    proven_size_t add = (proven_size_t)strlen(new_ext);

    proven_result_mem_mut_t res = proven_arena_alloc(arena, keep + add + 1);
    if (!proven_is_ok(res.err)) return empty;
    char *dst = (char*)res.value.ptr;
    memcpy(dst, path.ptr, keep);
    memcpy(dst + keep, new_ext, add);
    dst[keep + add] = '\0';
    return (proven_u8str_view_t){ .ptr = (const proven_byte_t *)dst, .size = keep + add };
}

proven_u8str_view_t rubraview_path_join(proven_arena_t *arena, proven_u8str_view_t dir, proven_u8str_view_t filename) {
    if (!arena) return PROVEN_LIT("");
    if (dir.size == 0) return filename;
    if (filename.size == 0) return dir;

    bool needs_sep = !rubraview_path_is_sep(dir.ptr[dir.size - 1]) && !rubraview_path_is_sep(filename.ptr[0]);
    proven_size_t total_len = dir.size + (needs_sep ? 1 : 0) + filename.size;

    /* Allocate total_len + 1 to enforce the null-terminated allocation invariant */
    proven_result_mem_mut_t res = proven_arena_alloc(arena, total_len + 1);
    if (!proven_is_ok(res.err)) {
        return PROVEN_LIT("");
    }

    char *dst = (char*)res.value.ptr;
    memcpy(dst, dir.ptr, dir.size);
    proven_size_t offset = dir.size;
    if (needs_sep) {
        dst[offset++] = '/';
    }
    memcpy(dst + offset, filename.ptr, filename.size);
    offset += filename.size;
    dst[offset] = '\0'; /* Guaranteed null terminator */

    return (proven_u8str_view_t){
        .ptr = (const proven_byte_t *)dst,
        .size = total_len
    };
}

bool rubraview_path_same(proven_u8str_view_t a, proven_u8str_view_t b) {
    if (a.size != b.size) return false;
    for (proven_size_t i = 0; i < a.size; ++i) {
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

proven_u8str_view_t rubraview_path_safe_name(char *buf, size_t cap, proven_u8str_view_t name) {
    proven_u8str_view_t none = PROVEN_LIT("");
    if (!buf || cap < 16) return none;
    proven_u8str_view_t base = rubraview_path_basename(name);

    /* Characters Windows refuses, and control bytes. */
    char clean[512];
    size_t n = 0;
    for (size_t i = 0; i < base.size && n < sizeof(clean); ++i) {
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
    return (proven_u8str_view_t){ .ptr = (const proven_byte_t *)buf, .size = at };
}

/* The next component of `p` from `*at`: false at the end. */
static bool path_part(proven_u8str_view_t p, proven_size_t *at, proven_u8str_view_t *out) {
    while (*at < p.size && rubraview_path_is_sep(p.ptr[*at])) (*at)++;
    if (*at >= p.size) return false;
    proven_size_t start = *at;
    while (*at < p.size && !rubraview_path_is_sep(p.ptr[*at])) (*at)++;
    *out = (proven_u8str_view_t){ .ptr = p.ptr + start, .size = *at - start };
    return true;
}

/* How a whole path begins: 2 "//server", 1 "/", 0 a drive or a bare name. */
static int path_lead(proven_u8str_view_t p) {
    if (p.size >= 2 && rubraview_path_is_sep(p.ptr[0]) && rubraview_path_is_sep(p.ptr[1])) return 2;
    return p.size >= 1 && rubraview_path_is_sep(p.ptr[0]) ? 1 : 0;
}

#define RELATIVE_PARTS 64

proven_u8str_view_t rubraview_path_relative(proven_arena_t *arena, proven_u8str_view_t base, proven_u8str_view_t path) {
    if (!arena || base.size == 0 || path.size == 0) return path;
    proven_u8str_view_t bs[RELATIVE_PARTS], ps[RELATIVE_PARTS];
    proven_size_t bn = 0, pn = 0, at = 0;
    while (bn < RELATIVE_PARTS && path_part(base, &at, &bs[bn])) bn++;
    if (at < base.size && bn == RELATIVE_PARTS) return path;
    at = 0;
    while (pn < RELATIVE_PARTS && path_part(path, &at, &ps[pn])) pn++;
    if ((at < path.size && pn == RELATIVE_PARTS) || pn == 0) return path;

    /* A drive or a share's server and name come first, and must be the same thing. */
    int lead = path_lead(path);
    if (path_lead(base) != lead) return path;
    proven_size_t fixed = lead == 2 ? 2 : lead == 0 ? 1 : 0;
    if (bn < fixed || pn <= fixed) return path;
    for (proven_size_t i = 0; i < fixed; ++i) if (!rubraview_path_same(bs[i], ps[i])) return path;

    /* The folders the two share; the last part of `path` is its file's name. */
    proven_size_t common = fixed;
    while (common < bn && common + 1 < pn && rubraview_path_same(bs[common], ps[common])) common++;
    proven_size_t ups = bn - common, len = ups * 3;
    for (proven_size_t i = common; i < pn; ++i) len += ps[i].size + 1;
    proven_result_mem_mut_t res = proven_arena_alloc(arena, len + 1);
    if (!proven_is_ok(res.err)) return path;
    char *dst = (char*)res.value.ptr;
    proven_size_t n = 0;
    for (proven_size_t i = 0; i < ups; ++i) { dst[n++] = '.'; dst[n++] = '.'; dst[n++] = '/'; }
    for (proven_size_t i = common; i < pn; ++i) {
        memcpy(dst + n, ps[i].ptr, ps[i].size);
        n += ps[i].size;
        if (i + 1 < pn) dst[n++] = '/';
    }
    dst[n] = '\0';
    return (proven_u8str_view_t){ .ptr = (const proven_byte_t *)dst, .size = n };
}

bool rubraview_path_state_temp(proven_u8str_view_t name, proven_u8str_view_t base, uint32_t *out_pid) {
    if (!name.ptr || !base.ptr || base.size == 0 || name.size < base.size + 8) return false;
    if (memcmp(name.ptr, base.ptr, base.size) != 0 || name.ptr[base.size] != '.') return false;
    if (memcmp(name.ptr + name.size - 4, ".tmp", 4) != 0) return false;
    size_t at = base.size + 1, end = name.size - 4;
    int64_t pid = 0;
    size_t digits = 0;
    for (; at < end && name.ptr[at] >= '0' && name.ptr[at] <= '9'; ++at, ++digits) {
        pid = rubraview_digit_append(pid, name.ptr[at]);
        if (pid > 0xFFFFFFFFll) return false;
    }
    if (digits == 0 || at >= end || name.ptr[at] != '-') return false;
    size_t ticks = 0;
    for (++at; at < end && name.ptr[at] >= '0' && name.ptr[at] <= '9'; ++at) ++ticks;
    if (ticks == 0 || at != end) return false;
    if (out_pid) *out_pid = (uint32_t)pid;
    return true;
}

proven_u8str_view_t rubraview_path_typed_folder(proven_u8str_view_t typed) {
    const char *p = (const char *)typed.ptr;
    size_t n = p ? typed.size : 0;
    while (n > 0 && (p[0] == ' ' || p[0] == '"' || p[0] == '\t')) { p++; n--; }
    while (n > 0 && (p[n - 1] == ' ' || p[n - 1] == '"' || p[n - 1] == '\t')) n--;
    while (n > 1 && rubraview_path_is_sep(p[n - 1]) && !(n == 3 && p[1] == ':')) n--;
    return proven_u8str_view_from_bytes(n ? p : "", n);
}
