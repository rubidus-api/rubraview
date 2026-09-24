#include "rubraview/pagesource.h"
#include "rubraview/glob.h"
#include "rubraview/path.h"
#include <string.h>

#define ARCHIVE_FILTER "*.cbz;*.zip;*.cb7;*.7z"
#define COMICINFO_NAME "ComicInfo.xml"

static bool name_is_comicinfo(u8str_t name) {
    u8str_t base = rubraview_path_basename(name);
    if (base.len != sizeof(COMICINFO_NAME) - 1) return false;
    /* The manifest sits in the archive root but archivers differ on
       case, so match it case-insensitively. */
    for (size_t i = 0; i < base.len; ++i) {
        char a = base.ptr[i], b = COMICINFO_NAME[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

typedef struct ref_buf {
    rubraview_page_ref_t *data;
    size_t count, capacity;
} ref_buf_t;

static bool ref_buf_push(proven_arena_t *arena, ref_buf_t *b, rubraview_page_ref_t ref) {
    if (b->count >= b->capacity) {
        size_t new_cap = b->capacity == 0 ? 32 : b->capacity * 2;
        proven_result_mem_mut_t res = rubraview_arena_alloc_array(arena, new_cap, sizeof(rubraview_page_ref_t));
        if (!proven_is_ok(res.err)) return false;
        rubraview_page_ref_t *data = (rubraview_page_ref_t*)(void*)res.value.ptr;
        if (b->data && b->count > 0) memcpy(data, b->data, b->count * sizeof(rubraview_page_ref_t));
        b->data = data;
        b->capacity = new_cap;
    }
    b->data[b->count++] = ref;
    return true;
}

rubraview_page_source_t rubraview_page_source_from_listing(proven_arena_t *arena,
                                                            const rubraview_fs_listing_t *listing,
                                                            u8str_t extension_filter,
                                                            rubraview_sort_mode_t mode,
                                                            bool ascending) {
    rubraview_page_source_t source = { .kind = RUBRAVIEW_PAGE_SOURCE_FOLDER };
    if (!arena || !listing || listing->count == 0) return source;

    rubraview_sibling_index_t index = rubraview_fs_index_siblings(
        arena, listing, (u8str_t){ .ptr = "", .len = 0 }, extension_filter, mode, ascending);
    if (index.count == 0) return source;

    ref_buf_t buf = {0};
    for (size_t i = 0; i < index.count; ++i) {
        rubraview_page_ref_t ref = {
            .name = rubraview_path_basename(index.paths[i]),
            .path = index.paths[i],
            .entry_index = 0,
        };
        if (!ref_buf_push(arena, &buf, ref)) break;
    }

    source.pages = buf.data;
    source.page_count = buf.count;
    return source;
}

static rubraview_page_source_t page_source_from_zip(proven_arena_t *arena,
                                                    const uint8_t *data, size_t size,
                                                    u8str_t archive_path,
                                                    u8str_t extension_filter,
                                                    rubraview_codepage_t override_choice,
                                                    uint32_t max_entry_bytes) {
    rubraview_page_source_t source = { .kind = RUBRAVIEW_PAGE_SOURCE_ARCHIVE, .archive_path = archive_path };
    if (!arena || !data || size == 0) return source;

    rubraview_zip_result_t opened = rubraview_zip_open(arena, data, size);
    if (opened.err != RUBRAVIEW_ZIP_OK) return source;
    source.archive = opened.value;

    /* Names first: a legacy archive's entries have to be readable before
       they can be filtered or sorted (§3.8.3). */
    proven_result_mem_mut_t names_res =
        rubraview_arena_alloc_array(arena, opened.value.entry_count, sizeof(u8str_t));
    if (!proven_is_ok(names_res.err)) return source;
    u8str_t *names = (u8str_t*)(void*)names_res.value.ptr;

    for (size_t i = 0; i < opened.value.entry_count; ++i) {
        const rubraview_zip_entry_t *entry = &opened.value.entries[i];
        names[i] = entry->name;

        uint32_t codepage = 0;
        if (rubraview_archive_filename_plan(entry->name, entry->utf8_flag, override_choice, &codepage)) {
            u8str_t decoded = rubraview_pal_transcode_codepage(arena, entry->name, codepage);
            /* A platform with no code-page tables returns nothing; the
               raw bytes are still better than an empty name. */
            if (decoded.len > 0) names[i] = decoded;
        }
    }

    ref_buf_t buf = {0};
    rubraview_sort_item_t *items = NULL;
    proven_result_mem_mut_t items_res =
        rubraview_arena_alloc_array(arena, opened.value.entry_count, sizeof(rubraview_sort_item_t));
    if (!proven_is_ok(items_res.err)) return source;
    items = (rubraview_sort_item_t*)(void*)items_res.value.ptr;

    size_t kept = 0;
    for (size_t i = 0; i < opened.value.entry_count; ++i) {
        if (name_is_comicinfo(names[i])) {
            /* §3.8.5: read the manifest now, while the archive is open. */
            rubraview_zip_data_result_t xml = rubraview_zip_read_entry(arena, &source.archive, i, max_entry_bytes);
            if (xml.err == RUBRAVIEW_ZIP_OK) {
                source.has_comicinfo = true;
                source.comicinfo_xml = xml.data;
            }
            continue;
        }

        /* A directory entry inside a ZIP is a name ending in '/'. */
        if (names[i].len > 0 && names[i].ptr[names[i].len - 1] == '/') continue;
        if (!rubraview_glob_match_list(rubraview_path_basename(names[i]), extension_filter)) continue;

        items[kept++] = (rubraview_sort_item_t){
            .name = names[i],
            .mtime = 0, .ctime = 0, .size_bytes = opened.value.entries[i].uncompressed_size,
            .tag = (uint64_t)i,
        };
    }

    /* §3.8.1: pages are numbered, so natural order is the right order. */
    rubraview_sort_items(items, kept, RUBRAVIEW_SORT_NAME_NATURAL, true, NULL);

    for (size_t i = 0; i < kept; ++i) {
        rubraview_page_ref_t ref = {
            .name = items[i].name,
            .path = { .ptr = "", .len = 0 },
            .entry_index = (size_t)items[i].tag,
        };
        if (!ref_buf_push(arena, &buf, ref)) break;
    }

    source.pages = buf.data;
    source.page_count = buf.count;
    return source;
}

/* 7z stores its names as UTF-16 and the SDK hands them back as UTF-8,
   so §3.8.3's code-page guessing has nothing to do here — the format
   never had the ambiguity ZIP has. */
static rubraview_page_source_t page_source_from_7z(proven_arena_t *arena,
                                                   const uint8_t *data, size_t size,
                                                   u8str_t archive_path,
                                                   u8str_t extension_filter,
                                                   uint32_t max_entry_bytes,
                                                   uint64_t max_block_bytes) {
    rubraview_page_source_t source = { .kind = RUBRAVIEW_PAGE_SOURCE_ARCHIVE_7Z, .archive_path = archive_path };

    rubraview_sz_result_t opened = rubraview_sz_open(arena, data, size, max_block_bytes);
    if (opened.err != RUBRAVIEW_SZ_OK) return source;
    source.archive7z = opened.value;

    proven_result_mem_mut_t items_res =
        rubraview_arena_alloc_array(arena, (opened.value.entry_count + 1), sizeof(rubraview_sort_item_t));
    if (!proven_is_ok(items_res.err)) return source;
    rubraview_sort_item_t *items = (rubraview_sort_item_t*)(void*)items_res.value.ptr;

    ref_buf_t buf = {0};
    size_t kept = 0;
    for (size_t i = 0; i < opened.value.entry_count; ++i) {
        u8str_t name = opened.value.entries[i].name;

        if (name_is_comicinfo(name)) {
            rubraview_sz_data_result_t xml = rubraview_sz_read_entry(arena, &source.archive7z, i, max_entry_bytes);
            if (xml.err == RUBRAVIEW_SZ_OK) {
                source.has_comicinfo = true;
                source.comicinfo_xml = xml.data;
            }
            continue;
        }

        if (!rubraview_glob_match_list(rubraview_path_basename(name), extension_filter)) continue;

        items[kept++] = (rubraview_sort_item_t){
            .name = name,
            .mtime = 0, .ctime = 0,
            .size_bytes = opened.value.entries[i].size > UINT32_MAX
                            ? UINT32_MAX : (uint32_t)opened.value.entries[i].size,
            .tag = (uint64_t)i,
        };
    }

    rubraview_sort_items(items, kept, RUBRAVIEW_SORT_NAME_NATURAL, true, NULL);

    for (size_t i = 0; i < kept; ++i) {
        rubraview_page_ref_t ref = {
            .name = items[i].name,
            .path = { .ptr = "", .len = 0 },
            .entry_index = (size_t)items[i].tag,
        };
        if (!ref_buf_push(arena, &buf, ref)) break;
    }

    source.pages = buf.data;
    source.page_count = buf.count;
    return source;
}

/* The extension is a hint, not evidence: a `.cbz` that is really a 7z
   happens often enough that the signature decides. */
static bool looks_like_7z(const uint8_t *data, size_t size) {
    static const uint8_t sig[6] = { '7', 'z', 0xBC, 0xAF, 0x27, 0x1C };
    return size >= sizeof(sig) && memcmp(data, sig, sizeof(sig)) == 0;
}

rubraview_page_source_t rubraview_page_source_from_archive(proven_arena_t *arena,
                                                            const uint8_t *data, size_t size,
                                                            u8str_t archive_path,
                                                            u8str_t extension_filter,
                                                            rubraview_codepage_t override_choice,
                                                            uint32_t max_entry_bytes,
                                                            uint64_t max_block_bytes) {
    if (arena && data && looks_like_7z(data, size)) {
        return page_source_from_7z(arena, data, size, archive_path, extension_filter,
                                   max_entry_bytes, max_block_bytes);
    }
    return page_source_from_zip(arena, data, size, archive_path, extension_filter,
                                override_choice, max_entry_bytes);
}

void rubraview_page_source_close(rubraview_page_source_t *source) {
    if (!source) return;
    if (source->kind == RUBRAVIEW_PAGE_SOURCE_ARCHIVE_7Z) rubraview_sz_close(&source->archive7z);
    source->pages = NULL;
    source->page_count = 0;
}

rubraview_page_bytes_t rubraview_page_source_read(proven_arena_t *arena,
                                                   rubraview_page_source_t *source,
                                                   size_t index,
                                                   uint32_t max_entry_bytes) {
    rubraview_page_bytes_t result = { .data = { .ptr = "", .len = 0 }, .from_disk = false, .ok = false };
    if (!source || index >= source->page_count) return result;

    if (source->kind == RUBRAVIEW_PAGE_SOURCE_FOLDER) {
        /* Nothing to decompress: the caller opens the path itself, which
           lets the image PAL decode straight from the file. */
        result.from_disk = true;
        result.ok = source->pages[index].path.len > 0;
        return result;
    }

    if (source->kind == RUBRAVIEW_PAGE_SOURCE_ARCHIVE_7Z) {
        rubraview_sz_data_result_t seven = rubraview_sz_read_entry(
            arena, &source->archive7z, source->pages[index].entry_index, max_entry_bytes);
        if (seven.err != RUBRAVIEW_SZ_OK) return result;
        result.data = seven.data;
        result.ok = true;
        return result;
    }

    rubraview_zip_data_result_t bytes = rubraview_zip_read_entry(
        arena, &source->archive, source->pages[index].entry_index, max_entry_bytes);
    if (bytes.err != RUBRAVIEW_ZIP_OK) return result;

    result.data = bytes.data;
    result.ok = true;
    return result;
}

int32_t rubraview_page_source_find(const rubraview_page_source_t *source, u8str_t path) {
    if (!source || path.len == 0) return -1;

    u8str_t wanted = rubraview_path_basename(path);
    if (wanted.len == 0) return -1;

    /* The name alone is what is compared. A full path, a relative path
       and a bare filename all name the same picture once the folder is
       open, and only the name survives all three. */
    for (size_t i = 0; i < source->page_count; ++i) {
        u8str_t candidate = source->pages[i].path.len > 0
                              ? rubraview_path_basename(source->pages[i].path)
                              : source->pages[i].name;
        if (rubraview_path_same(candidate, wanted)) return (int32_t)i;
    }
    return -1;
}

u8str_t rubraview_page_source_sibling_archive(proven_arena_t *arena,
                                              const rubraview_fs_listing_t *listing,
                                              u8str_t current_archive_path,
                                              bool forward) {
    u8str_t none = { .ptr = "", .len = 0 };
    if (!arena || !listing || listing->count == 0) return none;

    /* Order the directory's archives the way the reader sees them, then
       step one place — that is what makes Vol 01 run into Vol 02. */
    rubraview_sibling_index_t archives = rubraview_fs_index_siblings(
        arena, listing, current_archive_path, U8(ARCHIVE_FILTER), RUBRAVIEW_SORT_NAME_NATURAL, true);
    if (archives.count == 0 || !archives.found) return none;

    if (forward) {
        if (archives.current + 1 >= archives.count) return none;
        return archives.paths[archives.current + 1];
    }
    if (archives.current == 0) return none;
    return archives.paths[archives.current - 1];
}
