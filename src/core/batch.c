#include "rubraview/batch.h"
#include "rubraview/glob.h"
#include <stdio.h>
#include <string.h>

bool rubraview_batch_file_matches(const rubraview_batch_job_t *job, u8str_t filename, uint64_t file_size) {
    if (!job) return false;

    if (job->include_pattern.len > 0 && !rubraview_glob_match_list(filename, job->include_pattern)) {
        return false;
    }
    if (job->exclude_pattern.len > 0 && rubraview_glob_match_list(filename, job->exclude_pattern)) {
        return false;
    }
    if (job->min_size_bytes > 0 && file_size < job->min_size_bytes) {
        return false;
    }
    if (job->max_size_bytes > 0 && file_size > job->max_size_bytes) {
        return false;
    }
    return true;
}

typedef struct byte_buf {
    uint8_t *data;
    size_t len, cap;
} byte_buf_t;

static bool byte_buf_reserve(proven_arena_t *arena, byte_buf_t *b, size_t extra) {
    if (b->len + extra <= b->cap) return true;
    size_t new_cap = b->cap == 0 ? 128 : b->cap * 2;
    while (new_cap < b->len + extra) new_cap *= 2;

    proven_result_mem_mut_t res = proven_arena_alloc(arena, new_cap);
    if (!proven_is_ok(res.err)) return false;
    if (b->data && b->len > 0) memcpy(res.value.ptr, b->data, b->len);
    b->data = res.value.ptr;
    b->cap = new_cap;
    return true;
}

static bool byte_buf_append(proven_arena_t *arena, byte_buf_t *b, const char *s, size_t n) {
    if (n == 0) return true;
    if (!byte_buf_reserve(arena, b, n)) return false;
    memcpy(b->data + b->len, s, n);
    b->len += n;
    return true;
}

static bool byte_buf_append_str(proven_arena_t *arena, byte_buf_t *b, u8str_t s) {
    return byte_buf_append(arena, b, s.ptr, s.len);
}

u8str_t rubraview_batch_format_name(proven_arena_t *arena, u8str_t pattern, u8str_t name_stem, u8str_t ext, int32_t width, int32_t height, u8str_t date_str) {
    if (!arena) return (u8str_t){ .ptr = "", .len = 0 };

    /* rubraview_path_ext keeps the leading dot; the {ext} token does not. */
    u8str_t ext_no_dot = ext;
    if (ext_no_dot.len > 0 && ext_no_dot.ptr[0] == '.') {
        ext_no_dot.ptr += 1;
        ext_no_dot.len -= 1;
    }

    byte_buf_t buf = {0};
    size_t i = 0;
    while (i < pattern.len) {
        if (pattern.ptr[i] != '{') {
            byte_buf_append(arena, &buf, pattern.ptr + i, 1);
            i++;
            continue;
        }

        size_t close = i + 1;
        while (close < pattern.len && pattern.ptr[close] != '}') close++;
        if (close >= pattern.len) {
            /* No matching '}': copy the rest through unchanged. */
            byte_buf_append(arena, &buf, pattern.ptr + i, pattern.len - i);
            break;
        }

        const char *tok = pattern.ptr + i + 1;
        size_t tok_len = close - i - 1;

        if (tok_len == 4 && memcmp(tok, "name", 4) == 0) {
            byte_buf_append_str(arena, &buf, name_stem);
        } else if (tok_len == 3 && memcmp(tok, "ext", 3) == 0) {
            byte_buf_append_str(arena, &buf, ext_no_dot);
        } else if (tok_len == 1 && tok[0] == 'w') {
            char num[16];
            int n = snprintf(num, sizeof(num), "%d", width);
            if (n > 0) byte_buf_append(arena, &buf, num, (size_t)n);
        } else if (tok_len == 1 && tok[0] == 'h') {
            char num[16];
            int n = snprintf(num, sizeof(num), "%d", height);
            if (n > 0) byte_buf_append(arena, &buf, num, (size_t)n);
        } else if (tok_len == 4 && memcmp(tok, "date", 4) == 0) {
            byte_buf_append_str(arena, &buf, date_str);
        } else {
            /* Unrecognized token: copy the whole "{...}" through unchanged. */
            byte_buf_append(arena, &buf, pattern.ptr + i, close - i + 1);
        }

        i = close + 1;
    }

    proven_result_mem_mut_t res = proven_arena_alloc(arena, buf.len + 1);
    if (!proven_is_ok(res.err)) return (u8str_t){ .ptr = "", .len = 0 };
    if (buf.len > 0) memcpy(res.value.ptr, buf.data, buf.len);
    res.value.ptr[buf.len] = '\0';

    return (u8str_t){ .ptr = (const char*)res.value.ptr, .len = buf.len };
}
