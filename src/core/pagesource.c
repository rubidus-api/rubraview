#include "rubraview/pagesource.h"
#include "rubraview/glob.h"
#include "rubraview/path.h"
#include <stdlib.h>
#include <string.h>

#define ARCHIVE_FILTER "*.cbz;*.zip;*.cb7;*.7z;*.cbr;*.rar;*.alz"
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

    /* §3.8.1: pages are numbered, so natural order is the right order —
       folder by folder, the way the book's own folders read (owner,
       2026-09-28: comics with complicated paths). */
    rubraview_sort_items(items, kept, RUBRAVIEW_SORT_PATH_NATURAL, true, NULL);

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

    rubraview_sort_items(items, kept, RUBRAVIEW_SORT_PATH_NATURAL, true, NULL);   /* a CB7 reads the same, folder by folder */

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

/* RAR: RAR 4's names may be a code page's, as ZIP's are, and go through
   the same plan (§3.8.3); RAR 5's are UTF-8. A solid archive reads like a
   CB7's solid block, from the start of its chain (plan 2026-09-30-rar-reader). */
bool rubraview_page_source_is_later_volume(u8str_t name) {
    rubraview_rar_volume_name_kind_t kind = rubraview_rar_volume_name_kind(name);
    return kind == RUBRAVIEW_RAR_NAME_OLD || (kind == RUBRAVIEW_RAR_NAME_PART && rubraview_rar_volume_number(name) > 1);
}

/* The volumes of the set `archive_path` belongs to, mapped from the first
   while they are there (owner, 2026-10-01). 0 when it is not a set, or its
   first volume is not beside it: then the archive is read on its own. */
static size_t rar_map_set(proven_arena_t *arena, rubraview_page_source_t *source, const uint8_t *data, size_t size,
                          u8str_t archive_path) {
    rubraview_rar_volume_info_t info;
    (void)rubraview_rar_volume_info(data, size, &info);
    rubraview_rar_volume_name_kind_t kind = rubraview_rar_volume_name_kind(archive_path);
    if (archive_path.len == 0 || (!info.is_volume && kind == RUBRAVIEW_RAR_NAME_PLAIN)) return 0;
    bool old = kind == RUBRAVIEW_RAR_NAME_OLD || (kind == RUBRAVIEW_RAR_NAME_PLAIN && !info.new_numbering);
    u8str_t name = rubraview_rar_first_volume(arena, archive_path, old);
    size_t cap = 0;
    for (size_t i = 0; i < 999 && name.len > 0; ++i) {
        if (source->rar_map_count == cap) {
            size_t grown = cap ? cap * 2 : 8;
            rubraview_fs_mapping_t *maps = (rubraview_fs_mapping_t*)realloc(source->rar_maps, grown * sizeof(*maps));
            if (!maps) break;
            source->rar_maps = maps;
            cap = grown;
        }
        rubraview_fs_mapping_t map = {0};
        if (!rubraview_pal_fs_map(name, &map)) break;
        if (map.size > (uint64_t)SIZE_MAX) { rubraview_pal_fs_unmap(&map); break; }
        source->rar_maps[source->rar_map_count++] = map;
        name = rubraview_rar_next_volume(arena, name, old);
    }
    return source->rar_map_count;
}

static void rar_unmap_set(rubraview_page_source_t *source) {
    for (size_t i = 0; i < source->rar_map_count; ++i) rubraview_pal_fs_unmap(&source->rar_maps[i]);
    free(source->rar_maps);
    source->rar_maps = NULL;
    source->rar_map_count = 0;
}

static rubraview_page_source_t page_source_from_rar(proven_arena_t *arena,
                                                    const uint8_t *data, size_t size,
                                                    u8str_t archive_path,
                                                    u8str_t extension_filter,
                                                    rubraview_codepage_t override_choice,
                                                    uint32_t max_entry_bytes,
                                                    u8str_t password) {
    rubraview_page_source_t source = { .kind = RUBRAVIEW_PAGE_SOURCE_ARCHIVE_RAR, .archive_path = archive_path };
    rubraview_rar_volume_t one = { .data = data, .size = size };
    const rubraview_rar_volume_t *volumes = &one;
    size_t volume_count = 1;
    size_t mapped = rar_map_set(arena, &source, data, size, archive_path);
    if (mapped > 0) {
        proven_result_mem_mut_t vres = rubraview_arena_alloc_array(arena, mapped, sizeof(rubraview_rar_volume_t));
        if (proven_is_ok(vres.err)) {
            rubraview_rar_volume_t *vols = (rubraview_rar_volume_t*)(void*)vres.value.ptr;
            for (size_t i = 0; i < mapped; ++i) vols[i] = (rubraview_rar_volume_t){ source.rar_maps[i].data, (size_t)source.rar_maps[i].size };
            volumes = vols;
            volume_count = mapped;
        }
    }
    rubraview_rar_result_t opened = rubraview_rar_open_volumes(arena, volumes, volume_count, password);
    if (opened.err == RUBRAVIEW_RAR_ERR_NO_CODEC) { source.needs_codec = true; return source; }
    if (opened.err == RUBRAVIEW_RAR_ERR_ENCRYPTED || opened.err == RUBRAVIEW_RAR_ERR_BAD_PASSWORD) {
        source.needs_password = true;                          /* its headers are locked */
        source.password_wrong = opened.err == RUBRAVIEW_RAR_ERR_BAD_PASSWORD;
        return source;
    }
    if (opened.err != RUBRAVIEW_RAR_OK) return source;
    source.archiverar = opened.value;
    /* Without a RAR decoder only stored, unencrypted pages can be read:
       a book that needs more says so rather than opening half-blank. */
    for (size_t i = 0; i < source.archiverar.entry_count; ++i) {
        const rubraview_rar_entry_t *e = &source.archiverar.entries[i];
        if (rubraview_rar_needs_codec(&source.archiverar, i) &&
            rubraview_glob_match_list(rubraview_path_basename(e->name), extension_filter)) {
            source.needs_codec = true;
            return source;
        }
    }
    if (rubraview_rar_needs_password(&source.archiverar)) {
        /* Its pages are locked: the password is tried on one of them now. */
        rubraview_rar_err_t tried = password.len > 0 ? rubraview_rar_set_password(&source.archiverar, password)
                                                     : RUBRAVIEW_RAR_ERR_ENCRYPTED;
        if (tried != RUBRAVIEW_RAR_OK) {
            source.needs_password = true;
            source.password_wrong = password.len > 0 && tried == RUBRAVIEW_RAR_ERR_BAD_PASSWORD;
            return source;
        }
    }

    proven_result_mem_mut_t items_res =
        rubraview_arena_alloc_array(arena, (opened.value.entry_count + 1), sizeof(rubraview_sort_item_t));
    if (!proven_is_ok(items_res.err)) return source;
    rubraview_sort_item_t *items = (rubraview_sort_item_t*)(void*)items_res.value.ptr;

    ref_buf_t buf = {0};
    size_t kept = 0;
    for (size_t i = 0; i < opened.value.entry_count; ++i) {
        rubraview_rar_entry_t *entry = &source.archiverar.entries[i];
        if (entry->name_is_legacy) {
            uint32_t codepage = 0;
            if (rubraview_archive_filename_plan(entry->name, false, override_choice, &codepage)) {
                u8str_t decoded = rubraview_pal_transcode_codepage(arena, entry->name, codepage);
                if (decoded.len > 0) entry->name = decoded;
            }
        }
        u8str_t name = entry->name;
        if (name_is_comicinfo(name)) {
            rubraview_rar_data_result_t xml = rubraview_rar_read_entry(arena, &source.archiverar, i, max_entry_bytes);
            if (xml.err == RUBRAVIEW_RAR_OK) {
                source.has_comicinfo = true;
                source.comicinfo_xml = xml.data;
            }
            continue;
        }
        if (entry->crypt == RUBRAVIEW_RAR_CRYPT_OLD || !rubraview_glob_match_list(rubraview_path_basename(name), extension_filter)) continue;
        items[kept++] = (rubraview_sort_item_t){
            .name = name,
            .mtime = 0, .ctime = 0,
            .size_bytes = entry->size > UINT32_MAX ? UINT32_MAX : (uint32_t)entry->size,
            .tag = (uint64_t)i,
        };
    }
    rubraview_sort_items(items, kept, RUBRAVIEW_SORT_PATH_NATURAL, true, NULL);
    for (size_t i = 0; i < kept; ++i) {
        rubraview_page_ref_t ref = { .name = items[i].name, .path = { .ptr = "", .len = 0 }, .entry_index = (size_t)items[i].tag };
        if (!ref_buf_push(arena, &buf, ref)) break;
    }
    source.pages = buf.data;
    source.page_count = buf.count;
    return source;
}

/* ALZ (docs/specs/alz-format.md): names are a code page's bytes, as a legacy
   ZIP's are, with `\` between folders; every file is compressed on its own.
   A split archive's later volumes (`x.a00`, ...) are mapped here, from the
   `.alz` on, until one is missing (spec §2). */
static size_t alz_map_set(proven_arena_t *arena, rubraview_page_source_t *source, const uint8_t *data, size_t size,
                          u8str_t archive_path) {
    /* What ALZip writes tells more than the names (rubraview/alz.h): an
       archive that ends in its end record is whole, so a stray `.a00` beside
       it is not joined; a volume whose head names another number ends the
       set. A head that is not ALZip's is taken, as the spec says to. */
    if (rubraview_alz_volume_is_last(data, size)) return 0;
    size_t cap = 0;
    for (unsigned number = 1;; ++number) {
        u8str_t name = rubraview_alz_volume_path(arena, archive_path, number);
        if (name.len == 0) break;
        if (source->alz_map_count == cap) {
            size_t grown = cap ? cap * 2 : 8;
            rubraview_fs_mapping_t *maps = (rubraview_fs_mapping_t*)realloc(source->alz_maps, grown * sizeof(*maps));
            if (!maps) break;
            source->alz_maps = maps;
            cap = grown;
        }
        rubraview_fs_mapping_t map = {0};
        if (!rubraview_pal_fs_map(name, &map)) break;
        if (map.size > (uint64_t)SIZE_MAX) { rubraview_pal_fs_unmap(&map); break; }
        int32_t said = rubraview_alz_volume_number(map.data, (size_t)map.size);
        if (said >= 0 && said != (int32_t)number) { rubraview_pal_fs_unmap(&map); break; }
        source->alz_maps[source->alz_map_count++] = map;
        if (rubraview_alz_volume_is_last(map.data, (size_t)map.size)) break;
    }
    return source->alz_map_count;
}

static void alz_unmap_set(rubraview_page_source_t *source) {
    for (size_t i = 0; i < source->alz_map_count; ++i) rubraview_pal_fs_unmap(&source->alz_maps[i]);
    free(source->alz_maps);
    source->alz_maps = NULL;
    source->alz_map_count = 0;
}

/* An entry's name for display: decoded from its code page (§3.8.3), then
   `\` made `/`. Only after decoding: in Shift-JIS 0x5C can be a trail byte. */
static u8str_t alz_display_name(proven_arena_t *arena, u8str_t raw, rubraview_codepage_t override_choice) {
    u8str_t name = raw;
    uint32_t codepage = 0;
    if (rubraview_archive_filename_plan(raw, false, override_choice, &codepage)) {
        u8str_t decoded = rubraview_pal_transcode_codepage(arena, raw, codepage);
        if (decoded.len > 0) name = decoded;
    }
    if (!memchr(name.ptr, '\\', name.len)) return name;
    proven_result_mem_mut_t res = proven_arena_alloc(arena, name.len + 1);
    if (!proven_is_ok(res.err)) return name;
    char *out = (char*)res.value.ptr;
    for (size_t i = 0; i < name.len; ++i) out[i] = name.ptr[i] == '\\' ? '/' : name.ptr[i];
    out[name.len] = '\0';
    return (u8str_t){ .ptr = out, .len = name.len };
}

/* ALZip encrypts with the password's bytes in its maker's code page (CP949
   for a Korean one), and the box gives UTF-8: a password that is not ASCII
   is tried as typed, then in the reader's chosen code page, CP949 and the
   system's. Each copy is wiped once tried. */
static rubraview_alz_err_t alz_try_password(proven_arena_t *arena, rubraview_alz_archive_t *archive, u8str_t password,
                                            rubraview_codepage_t override_choice) {
    rubraview_alz_err_t err = rubraview_alz_set_password(archive, password);
    bool ascii = true;
    for (size_t i = 0; i < password.len; ++i) ascii = ascii && (unsigned char)password.ptr[i] < 0x80;
    if (err != RUBRAVIEW_ALZ_ERR_BAD_PASSWORD || ascii) return err;
    const uint32_t pages[3] = { rubraview_codepage_id(override_choice), 949, 0 };
    for (size_t i = 0; i < 3; ++i) {
        if (pages[i] == 65001 || (i > 0 && pages[i] == pages[0]) || (i == 2 && pages[2] == pages[1])) continue;
        u8str_t bytes = rubraview_pal_encode_codepage(arena, password, pages[i]);
        if (bytes.len == 0) continue;
        err = rubraview_alz_set_password(archive, bytes);
        volatile char *wipe = (volatile char*)(uintptr_t)bytes.ptr;
        for (size_t k = 0; k < bytes.len; ++k) wipe[k] = 0;
        if (err != RUBRAVIEW_ALZ_ERR_BAD_PASSWORD) return err;
    }
    return RUBRAVIEW_ALZ_ERR_BAD_PASSWORD;
}

static rubraview_page_source_t page_source_from_alz(proven_arena_t *arena,
                                                    const uint8_t *data, size_t size,
                                                    u8str_t archive_path,
                                                    u8str_t extension_filter,
                                                    rubraview_codepage_t override_choice,
                                                    uint32_t max_entry_bytes,
                                                    u8str_t password) {
    rubraview_page_source_t source = { .kind = RUBRAVIEW_PAGE_SOURCE_ARCHIVE_ALZ, .archive_path = archive_path };
    size_t later = alz_map_set(arena, &source, data, size, archive_path);
    proven_result_mem_mut_t vres = rubraview_arena_alloc_array(arena, later + 1, sizeof(rubraview_alz_volume_t));
    if (!proven_is_ok(vres.err)) return source;
    rubraview_alz_volume_t *vols = (rubraview_alz_volume_t*)(void*)vres.value.ptr;
    vols[0] = (rubraview_alz_volume_t){ data, size };
    for (size_t i = 0; i < later; ++i) vols[i + 1] = (rubraview_alz_volume_t){ source.alz_maps[i].data, (size_t)source.alz_maps[i].size };

    rubraview_alz_result_t opened = rubraview_alz_open_volumes(arena, vols, later + 1);
    if (opened.err != RUBRAVIEW_ALZ_OK) return source;
    source.archivealz = opened.value;
    if (rubraview_alz_needs_password(&source.archivealz)) {
        /* Its pages are locked: the password is tried on them now. */
        rubraview_alz_err_t tried = password.len > 0 ? alz_try_password(arena, &source.archivealz, password, override_choice)
                                                     : RUBRAVIEW_ALZ_ERR_ENCRYPTED;
        if (tried != RUBRAVIEW_ALZ_OK) {
            source.needs_password = true;
            source.password_wrong = password.len > 0 && tried == RUBRAVIEW_ALZ_ERR_BAD_PASSWORD;
            return source;
        }
    }

    proven_result_mem_mut_t items_res =
        rubraview_arena_alloc_array(arena, opened.value.entry_count + 1, sizeof(rubraview_sort_item_t));
    if (!proven_is_ok(items_res.err)) return source;
    rubraview_sort_item_t *items = (rubraview_sort_item_t*)(void*)items_res.value.ptr;

    ref_buf_t buf = {0};
    size_t kept = 0;
    for (size_t i = 0; i < opened.value.entry_count; ++i) {
        const rubraview_alz_entry_t *entry = &source.archivealz.entries[i];
        if (entry->truncated) continue;                /* cut off: nothing whole to show */
        u8str_t name = alz_display_name(arena, entry->name, override_choice);
        if (name_is_comicinfo(name)) {
            rubraview_alz_data_result_t xml = rubraview_alz_read_entry(arena, &source.archivealz, i, max_entry_bytes);
            if (xml.err == RUBRAVIEW_ALZ_OK) {
                source.has_comicinfo = true;
                source.comicinfo_xml = xml.data;
            }
            continue;
        }
        if (!rubraview_glob_match_list(rubraview_path_basename(name), extension_filter)) continue;
        items[kept++] = (rubraview_sort_item_t){
            .name = name,
            .mtime = 0, .ctime = 0,
            .size_bytes = entry->size > UINT32_MAX ? UINT32_MAX : (uint32_t)entry->size,
            .tag = (uint64_t)i,
        };
    }
    rubraview_sort_items(items, kept, RUBRAVIEW_SORT_PATH_NATURAL, true, NULL);
    for (size_t i = 0; i < kept; ++i) {
        rubraview_page_ref_t ref = { .name = items[i].name, .path = { .ptr = "", .len = 0 }, .entry_index = (size_t)items[i].tag };
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
    return rubraview_page_source_from_archive_password(arena, data, size, archive_path, extension_filter, override_choice,
                                                       max_entry_bytes, max_block_bytes, (u8str_t){ .ptr = "", .len = 0 });
}

rubraview_page_source_t rubraview_page_source_from_archive_password(proven_arena_t *arena,
                                                                     const uint8_t *data, size_t size,
                                                                     u8str_t archive_path,
                                                                     u8str_t extension_filter,
                                                                     rubraview_codepage_t override_choice,
                                                                     uint32_t max_entry_bytes,
                                                                     uint64_t max_block_bytes,
                                                                     u8str_t password) {
    if (arena && data && looks_like_7z(data, size)) {
        return page_source_from_7z(arena, data, size, archive_path, extension_filter,
                                   max_entry_bytes, max_block_bytes);
    }
    if (arena && data && size >= 7 && data[0] == 'R' && rubraview_rar_is_rar(data, size < 64 ? size : 64)) {
        return page_source_from_rar(arena, data, size, archive_path, extension_filter, override_choice, max_entry_bytes, password);
    }
    if (arena && rubraview_alz_is_alz(data, size)) {
        return page_source_from_alz(arena, data, size, archive_path, extension_filter, override_choice, max_entry_bytes, password);
    }
    return page_source_from_zip(arena, data, size, archive_path, extension_filter,
                                override_choice, max_entry_bytes);
}

void rubraview_page_source_close(rubraview_page_source_t *source) {
    if (!source) return;
    if (source->kind == RUBRAVIEW_PAGE_SOURCE_ARCHIVE_7Z) rubraview_sz_close(&source->archive7z);
    if (source->kind == RUBRAVIEW_PAGE_SOURCE_ARCHIVE_RAR) {
        rubraview_rar_close(&source->archiverar);
        rar_unmap_set(source);
    }
    if (source->kind == RUBRAVIEW_PAGE_SOURCE_ARCHIVE_ALZ) {
        rubraview_alz_close(&source->archivealz);
        alz_unmap_set(source);
    }
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

    if (source->kind == RUBRAVIEW_PAGE_SOURCE_ARCHIVE_RAR) {
        rubraview_rar_data_result_t rar = rubraview_rar_read_entry(
            arena, &source->archiverar, source->pages[index].entry_index, max_entry_bytes);
        if (rar.err != RUBRAVIEW_RAR_OK) return result;
        result.data = rar.data;
        result.ok = true;
        return result;
    }

    if (source->kind == RUBRAVIEW_PAGE_SOURCE_ARCHIVE_ALZ) {
        rubraview_alz_data_result_t alz = rubraview_alz_read_entry(
            arena, &source->archivealz, source->pages[index].entry_index, max_entry_bytes);
        if (alz.err != RUBRAVIEW_ALZ_OK) return result;
        result.data = alz.data;
        result.ok = true;
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

uint64_t rubraview_page_source_entry_size(const rubraview_page_source_t *source, size_t index) {
    if (!source || index >= source->page_count) return 0;
    size_t entry = source->pages[index].entry_index;
    if (source->kind == RUBRAVIEW_PAGE_SOURCE_ARCHIVE_7Z) {
        return entry < source->archive7z.entry_count ? source->archive7z.entries[entry].size : 0;
    }
    if (source->kind == RUBRAVIEW_PAGE_SOURCE_ARCHIVE_RAR) {
        return entry < source->archiverar.entry_count ? source->archiverar.entries[entry].size : 0;
    }
    if (source->kind == RUBRAVIEW_PAGE_SOURCE_ARCHIVE_ALZ) {
        return entry < source->archivealz.entry_count ? source->archivealz.entries[entry].size : 0;
    }
    if (source->kind == RUBRAVIEW_PAGE_SOURCE_ARCHIVE) {
        return entry < source->archive.entry_count ? source->archive.entries[entry].uncompressed_size : 0;
    }
    return 0;
}

size_t rubraview_page_source_read_budget(const rubraview_page_source_t *source, size_t index) {
    uint64_t size = rubraview_page_source_entry_size(source, index);
    if (size == 0 || size > SIZE_MAX - 256) return 0;
    /* Both readers make one allocation of size + 1; the rest is alignment. */
    return (size_t)size + 256;
}

uint64_t rubraview_page_source_read_cost(const rubraview_page_source_t *source, size_t index) {
    if (!source || index >= source->page_count) return 0;
    if (source->kind == RUBRAVIEW_PAGE_SOURCE_ARCHIVE_RAR)
        return rubraview_rar_read_cost(&source->archiverar, source->pages[index].entry_index);
    if (source->kind != RUBRAVIEW_PAGE_SOURCE_ARCHIVE_7Z) return 0;
    return rubraview_sz_read_cost(&source->archive7z, source->pages[index].entry_index);
}

void rubraview_page_source_cancel(rubraview_page_source_t *source, bool cancel) {
    if (source && source->kind == RUBRAVIEW_PAGE_SOURCE_ARCHIVE_7Z) rubraview_sz_cancel(&source->archive7z, cancel);
    if (source && source->kind == RUBRAVIEW_PAGE_SOURCE_ARCHIVE_RAR) rubraview_rar_cancel(&source->archiverar, cancel);
}

void rubraview_page_source_progress(const rubraview_page_source_t *source, uint64_t *out_done, uint64_t *out_total) {
    if (out_done) *out_done = 0;
    if (out_total) *out_total = 0;
    if (source && source->kind == RUBRAVIEW_PAGE_SOURCE_ARCHIVE_7Z) rubraview_sz_progress(&source->archive7z, out_done, out_total);
    if (source && source->kind == RUBRAVIEW_PAGE_SOURCE_ARCHIVE_RAR) rubraview_rar_progress(&source->archiverar, out_done, out_total);
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

    /* A RAR set is one book (owner, 2026-10-01): its later volumes are stepped over. */
    size_t at = archives.current;
    for (;;) {
        if (forward ? at + 1 >= archives.count : at == 0) return none;
        at = forward ? at + 1 : at - 1;
        if (!rubraview_page_source_is_later_volume(archives.paths[at])) return archives.paths[at];
    }
}
