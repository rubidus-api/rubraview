#include "rubraview/comicinfo.h"
#include <stdio.h>
#include <string.h>

static u8str_t trim_ws(const char *ptr, size_t len) {
    size_t s = 0, e = len;
    while (s < e && (ptr[s] == ' ' || ptr[s] == '\t' || ptr[s] == '\n' || ptr[s] == '\r')) s++;
    while (e > s && (ptr[e - 1] == ' ' || ptr[e - 1] == '\t' || ptr[e - 1] == '\n' || ptr[e - 1] == '\r')) e--;
    return (u8str_t){ .ptr = ptr + s, .len = e - s };
}

static int32_t parse_int_or(u8str_t s, int32_t default_value) {
    if (s.len == 0) return default_value;
    size_t i = 0;
    bool neg = false;
    if (s.ptr[0] == '-' || s.ptr[0] == '+') { neg = (s.ptr[0] == '-'); i = 1; }
    if (i >= s.len) return default_value;

    int32_t result = 0;
    for (; i < s.len; ++i) {
        if (s.ptr[i] < '0' || s.ptr[i] > '9') return default_value;
        result = result * 10 + (s.ptr[i] - '0');
    }
    return neg ? -result : result;
}

/* Finds the first <tag>...</tag> pair and returns its trimmed inner text.
   Scoped to ComicInfo.xml's scalar fields, which never carry attributes;
   a self-closing <tag/> (empty value) is treated as absent. */
static bool find_scalar_element(u8str_t xml, const char *tag, u8str_t *out_text) {
    char open_buf[32], close_buf[32];
    int open_len = snprintf(open_buf, sizeof(open_buf), "<%s>", tag);
    int close_len = snprintf(close_buf, sizeof(close_buf), "</%s>", tag);
    if (open_len <= 0 || close_len <= 0) return false;

    for (size_t i = 0; i + (size_t)open_len <= xml.len; ++i) {
        if (memcmp(xml.ptr + i, open_buf, (size_t)open_len) != 0) continue;

        size_t content_start = i + (size_t)open_len;
        for (size_t j = content_start; j + (size_t)close_len <= xml.len; ++j) {
            if (memcmp(xml.ptr + j, close_buf, (size_t)close_len) == 0) {
                *out_text = trim_ws(xml.ptr + content_start, j - content_start);
                return true;
            }
        }
        return false; /* opening tag with no matching close */
    }
    return false;
}

/* Finds attr="value" within a bounded span (an element's attribute list),
   requiring the name to start at a whitespace boundary so "Type" does not
   match inside some other "XType" attribute. */
static bool find_attr_in_span(const char *ptr, size_t len, const char *attr_name, u8str_t *out_value) {
    size_t name_len = strlen(attr_name);
    for (size_t i = 0; i + name_len + 2 < len; ++i) {
        bool boundary_ok = (i == 0) || ptr[i - 1] == ' ' || ptr[i - 1] == '\t' || ptr[i - 1] == '\n' || ptr[i - 1] == '\r';
        if (!boundary_ok) continue;
        if (memcmp(ptr + i, attr_name, name_len) != 0) continue;

        size_t after = i + name_len;
        if (after >= len || ptr[after] != '=') continue;
        after++;
        if (after >= len || ptr[after] != '"') continue;
        after++;

        size_t start = after;
        while (after < len && ptr[after] != '"') after++;
        if (after >= len) return false;

        *out_value = (u8str_t){ .ptr = ptr + start, .len = after - start };
        return true;
    }
    return false;
}

typedef struct page_buf {
    rubraview_comicinfo_page_t *data;
    size_t count;
    size_t capacity;
} page_buf_t;

static bool page_buf_push(proven_arena_t *arena, page_buf_t *b, rubraview_comicinfo_page_t page) {
    if (b->count >= b->capacity) {
        size_t new_cap = b->capacity == 0 ? 8 : b->capacity * 2;
        proven_result_mem_mut_t res = proven_arena_alloc(arena, new_cap * sizeof(rubraview_comicinfo_page_t));
        if (!proven_is_ok(res.err)) return false;
        rubraview_comicinfo_page_t *new_data = (rubraview_comicinfo_page_t*)(void*)res.value.ptr;
        if (b->data && b->count > 0) memcpy(new_data, b->data, b->count * sizeof(rubraview_comicinfo_page_t));
        b->data = new_data;
        b->capacity = new_cap;
    }
    b->data[b->count++] = page;
    return true;
}

static void parse_pages(proven_arena_t *arena, u8str_t xml, rubraview_comicinfo_t *out) {
    page_buf_t buf = {0};
    size_t search_from = 0;

    while (search_from + 5 <= xml.len) {
        size_t found = (size_t)-1;
        for (size_t i = search_from; i + 5 <= xml.len; ++i) {
            if (memcmp(xml.ptr + i, "<Page", 5) != 0) continue;
            char after = (i + 5 < xml.len) ? xml.ptr[i + 5] : '\0';
            if (after == ' ' || after == '\t' || after == '\n' || after == '\r' || after == '/' || after == '>') {
                found = i;
                break;
            }
        }
        if (found == (size_t)-1) break;

        size_t j = found + 5;
        bool in_quotes = false;
        size_t tag_end = (size_t)-1;
        while (j < xml.len) {
            char c = xml.ptr[j];
            if (c == '"') in_quotes = !in_quotes;
            else if (!in_quotes && c == '>') { tag_end = j; break; }
            j++;
        }
        if (tag_end == (size_t)-1) break; /* unterminated tag, stop scanning */

        size_t attr_start = found + 5;
        size_t attr_len = tag_end - attr_start;

        rubraview_comicinfo_page_t page = { .image_index = -1, .type = { .ptr = "", .len = 0 } };
        u8str_t image_val;
        if (find_attr_in_span(xml.ptr + attr_start, attr_len, "Image", &image_val)) {
            page.image_index = parse_int_or(image_val, -1);
        }
        u8str_t type_val;
        if (find_attr_in_span(xml.ptr + attr_start, attr_len, "Type", &type_val)) {
            page.type = type_val;
        }

        page_buf_push(arena, &buf, page);
        search_from = tag_end + 1;
    }

    out->pages = buf.data;
    out->page_count = buf.count;
}

rubraview_comicinfo_t rubraview_comicinfo_parse(proven_arena_t *arena, u8str_t xml) {
    rubraview_comicinfo_t out = {0};
    out.manga = RUBRAVIEW_MANGA_NO;
    out.volume = -1;
    out.title = (u8str_t){ .ptr = "", .len = 0 };
    out.series = (u8str_t){ .ptr = "", .len = 0 };

    if (!arena || !xml.ptr || xml.len == 0) return out;

    u8str_t manga_raw;
    if (find_scalar_element(xml, "Manga", &manga_raw)) {
        if (manga_raw.len == 17 && memcmp(manga_raw.ptr, "YesAndRightToLeft", 17) == 0) {
            out.manga = RUBRAVIEW_MANGA_YES_RTL;
        } else if (manga_raw.len == 3 && memcmp(manga_raw.ptr, "Yes", 3) == 0) {
            out.manga = RUBRAVIEW_MANGA_YES;
        } else {
            out.manga = RUBRAVIEW_MANGA_NO;
        }
    }

    u8str_t title_raw;
    if (find_scalar_element(xml, "Title", &title_raw)) out.title = title_raw;

    u8str_t series_raw;
    if (find_scalar_element(xml, "Series", &series_raw)) out.series = series_raw;

    u8str_t volume_raw;
    if (find_scalar_element(xml, "Volume", &volume_raw)) out.volume = parse_int_or(volume_raw, -1);

    parse_pages(arena, xml, &out);

    return out;
}
