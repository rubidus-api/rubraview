#include "rubraview/comicinfo.h"
#include "rubraview/number.h"
#include <stdio.h>
#include <string.h>


static int32_t parse_int_or(proven_u8str_view_t s, int32_t default_value) {
    if (s.size == 0) return default_value;
    size_t i = 0;
    bool neg = false;
    if (s.ptr[0] == '-' || s.ptr[0] == '+') { neg = (s.ptr[0] == '-'); i = 1; }
    if (i >= s.size) return default_value;

    int32_t result = 0;
    for (; i < s.size; ++i) {
        if (s.ptr[i] < '0' || s.ptr[i] > '9') return default_value;
        result = rubraview_digit_append_i32(result, s.ptr[i]);
    }
    return neg ? -result : result;
}

/* Finds the first <tag>...</tag> pair and returns its trimmed inner text.
   Scoped to ComicInfo.xml's scalar fields, which never carry attributes;
   a self-closing <tag/> (empty value) is treated as absent. */
static bool find_scalar_element(proven_u8str_view_t xml, const char *tag, proven_u8str_view_t *out_text) {
    char open_buf[32], close_buf[32];
    int open_len = snprintf(open_buf, sizeof(open_buf), "<%s>", tag);
    int close_len = snprintf(close_buf, sizeof(close_buf), "</%s>", tag);
    if (open_len <= 0 || close_len <= 0) return false;

    for (size_t i = 0; i + (size_t)open_len <= xml.size; ++i) {
        if (memcmp(xml.ptr + i, open_buf, (size_t)open_len) != 0) continue;

        size_t content_start = i + (size_t)open_len;
        for (size_t j = content_start; j + (size_t)close_len <= xml.size; ++j) {
            if (memcmp(xml.ptr + j, close_buf, (size_t)close_len) == 0) {
                *out_text = proven_u8str_view_trim(proven_u8str_view_from_bytes((const char *)xml.ptr + content_start, j - content_start));
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
static bool find_attr_in_span(const char *ptr, size_t len, const char *attr_name, proven_u8str_view_t *out_value) {
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

        *out_value = (proven_u8str_view_t){ .ptr = (const proven_byte_t *)ptr + start, .size = after - start };
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
        proven_result_mem_mut_t res = rubraview_arena_alloc_array(arena, new_cap, sizeof(rubraview_comicinfo_page_t));
        if (!proven_is_ok(res.err)) return false;
        rubraview_comicinfo_page_t *new_data = (rubraview_comicinfo_page_t*)(void*)res.value.ptr;
        if (b->data && b->count > 0) memcpy(new_data, b->data, b->count * sizeof(rubraview_comicinfo_page_t));
        b->data = new_data;
        b->capacity = new_cap;
    }
    b->data[b->count++] = page;
    return true;
}

static void parse_pages(proven_arena_t *arena, proven_u8str_view_t xml, rubraview_comicinfo_t *out) {
    page_buf_t buf = {0};
    size_t search_from = 0;

    while (search_from + 5 <= xml.size) {
        size_t found = (size_t)-1;
        for (size_t i = search_from; i + 5 <= xml.size; ++i) {
            if (memcmp(xml.ptr + i, "<Page", 5) != 0) continue;
            char after = (i + 5 < xml.size) ? xml.ptr[i + 5] : '\0';
            if (after == ' ' || after == '\t' || after == '\n' || after == '\r' || after == '/' || after == '>') {
                found = i;
                break;
            }
        }
        if (found == (size_t)-1) break;

        size_t j = found + 5;
        bool in_quotes = false;
        size_t tag_end = (size_t)-1;
        while (j < xml.size) {
            char c = xml.ptr[j];
            if (c == '"') in_quotes = !in_quotes;
            else if (!in_quotes && c == '>') { tag_end = j; break; }
            j++;
        }
        if (tag_end == (size_t)-1) break; /* unterminated tag, stop scanning */

        size_t attr_start = found + 5;
        size_t attr_len = tag_end - attr_start;

        rubraview_comicinfo_page_t page = { .image_index = -1, .type = PROVEN_LIT("") };
        proven_u8str_view_t image_val;
        if (find_attr_in_span((const char *)xml.ptr + attr_start, attr_len, "Image", &image_val)) {
            page.image_index = parse_int_or(image_val, -1);
        }
        proven_u8str_view_t type_val;
        if (find_attr_in_span((const char *)xml.ptr + attr_start, attr_len, "Type", &type_val)) {
            page.type = type_val;
        }

        page_buf_push(arena, &buf, page);
        search_from = tag_end + 1;
    }

    out->pages = buf.data;
    out->page_count = buf.count;
}

rubraview_comicinfo_t rubraview_comicinfo_parse(proven_arena_t *arena, proven_u8str_view_t xml) {
    rubraview_comicinfo_t out = {0};
    out.manga = RUBRAVIEW_MANGA_NO;
    out.volume = -1;
    out.title = PROVEN_LIT("");
    out.series = PROVEN_LIT("");

    if (!arena || !xml.ptr || xml.size == 0) return out;

    proven_u8str_view_t manga_raw;
    if (find_scalar_element(xml, "Manga", &manga_raw)) {
        if (proven_u8str_view_eq(manga_raw, PROVEN_LIT("YesAndRightToLeft"))) {
            out.manga = RUBRAVIEW_MANGA_YES_RTL;
        } else if (proven_u8str_view_eq(manga_raw, PROVEN_LIT("Yes"))) {
            out.manga = RUBRAVIEW_MANGA_YES;
        } else {
            out.manga = RUBRAVIEW_MANGA_NO;
        }
    }

    proven_u8str_view_t title_raw;
    if (find_scalar_element(xml, "Title", &title_raw)) out.title = title_raw;

    proven_u8str_view_t series_raw;
    if (find_scalar_element(xml, "Series", &series_raw)) out.series = series_raw;

    proven_u8str_view_t volume_raw;
    if (find_scalar_element(xml, "Volume", &volume_raw)) out.volume = parse_int_or(volume_raw, -1);

    parse_pages(arena, xml, &out);

    return out;
}

static bool type_is_cover(proven_u8str_view_t type) {
    static const char FRONT[] = "FrontCover";
    static const char INNER[] = "InnerCover";
    if (type.size == sizeof(FRONT) - 1 && memcmp(type.ptr, FRONT, sizeof(FRONT) - 1) == 0) return true;
    if (type.size == sizeof(INNER) - 1 && memcmp(type.ptr, INNER, sizeof(INNER) - 1) == 0) return true;
    return false;
}

void rubraview_comicinfo_apply(const rubraview_comicinfo_t *info,
                               rubraview_layout_opts_t *opts,
                               rubraview_page_info_t *pages,
                               size_t page_count) {
    if (!info) return;

    if (opts && info->manga == RUBRAVIEW_MANGA_YES_RTL) {
        opts->mode = RUBRAVIEW_PAGE_LAYOUT_BOOK;
        opts->direction = RUBRAVIEW_READING_RTL;
    }

    if (!pages || page_count == 0 || !info->pages) return;

    for (size_t i = 0; i < info->page_count; ++i) {
        int32_t index = info->pages[i].image_index;
        if (index < 0 || (size_t)index >= page_count) continue;
        if (type_is_cover(info->pages[i].type)) {
            pages[index].force_standalone = true;
        }
    }
}
