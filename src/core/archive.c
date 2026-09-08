#include "rubraview/archive.h"
#include <string.h>

/* miniz supplies the inflate algorithm only (owner decision D-2); the
   container parsing below is this module's own. Nothing else in the tree
   includes it, per the RFC-0001 §8.1 one-module rule. */
#include "miniz.h"

#define ZIP_SIG_EOCD 0x06054b50u
#define ZIP_SIG_CENTRAL_HEADER 0x02014b50u
#define ZIP_SIG_LOCAL_HEADER 0x04034b50u

#define ZIP_EOCD_MIN_SIZE 22u
#define ZIP_CENTRAL_HEADER_MIN_SIZE 46u
#define ZIP_LOCAL_HEADER_MIN_SIZE 30u
#define ZIP_MAX_COMMENT_LEN 65535u
#define ZIP_GP_FLAG_UTF8 0x0800u
#define ZIP_METHOD_STORED 0u
#define ZIP_METHOD_DEFLATE 8u

static uint16_t rd_u16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t rd_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Scan backward for the End-of-Central-Directory signature. The EOCD is
   fixed-size plus a variable comment (0-65535 bytes), so it can only be
   found within the last (22 + 65535) bytes of the file. */
static bool find_eocd(const uint8_t *data, size_t size, size_t *out_offset) {
    if (size < ZIP_EOCD_MIN_SIZE) return false;

    size_t max_window = ZIP_EOCD_MIN_SIZE + ZIP_MAX_COMMENT_LEN;
    size_t search_window = size < max_window ? size : max_window;
    size_t earliest = size - search_window;
    size_t i = size - ZIP_EOCD_MIN_SIZE; /* last offset where a 22-byte EOCD still fits */

    for (;;) {
        if (rd_u32(data + i) == ZIP_SIG_EOCD) {
            *out_offset = i;
            return true;
        }
        if (i == earliest) return false;
        i--;
    }
}

rubraview_zip_result_t rubraview_zip_open(proven_arena_t *arena, const uint8_t *data, size_t size) {
    rubraview_zip_result_t result = {0};
    if (!arena || !data) {
        result.err = RUBRAVIEW_ZIP_ERR_NOT_A_ZIP;
        return result;
    }

    size_t eocd_off;
    if (!find_eocd(data, size, &eocd_off)) {
        result.err = RUBRAVIEW_ZIP_ERR_NOT_A_ZIP;
        return result;
    }

    const uint8_t *eocd = data + eocd_off;
    uint16_t total_entries = rd_u16(eocd + 10);
    uint32_t cd_size = rd_u32(eocd + 12);
    uint32_t cd_offset = rd_u32(eocd + 16);

    if ((uint64_t)cd_offset + (uint64_t)cd_size > (uint64_t)eocd_off) {
        result.err = RUBRAVIEW_ZIP_ERR_TRUNCATED;
        return result;
    }

    rubraview_zip_entry_t *entries = NULL;
    if (total_entries > 0) {
        proven_result_mem_mut_t res = proven_arena_alloc(arena, (size_t)total_entries * sizeof(rubraview_zip_entry_t));
        if (!proven_is_ok(res.err)) {
            result.err = RUBRAVIEW_ZIP_ERR_OUT_OF_MEMORY;
            return result;
        }
        entries = (rubraview_zip_entry_t*)(void*)res.value.ptr;
    }

    const uint8_t *cd_end = data + cd_offset + cd_size;
    const uint8_t *p = data + cd_offset;
    size_t parsed = 0;

    while (parsed < total_entries) {
        if (p + ZIP_CENTRAL_HEADER_MIN_SIZE > cd_end) {
            result.err = RUBRAVIEW_ZIP_ERR_TRUNCATED;
            return result;
        }
        if (rd_u32(p) != ZIP_SIG_CENTRAL_HEADER) {
            result.err = RUBRAVIEW_ZIP_ERR_CORRUPT;
            return result;
        }

        uint16_t gp_flags = rd_u16(p + 8);
        uint16_t method = rd_u16(p + 10);
        uint32_t uncompressed_size = rd_u32(p + 24);
        uint32_t compressed_size = rd_u32(p + 20);
        uint16_t filename_len = rd_u16(p + 28);
        uint16_t extra_len = rd_u16(p + 30);
        uint16_t comment_len = rd_u16(p + 32);
        uint32_t local_header_offset = rd_u32(p + 42);

        const uint8_t *record_end = p + ZIP_CENTRAL_HEADER_MIN_SIZE + filename_len + extra_len + comment_len;
        if (record_end > cd_end) {
            result.err = RUBRAVIEW_ZIP_ERR_TRUNCATED;
            return result;
        }

        entries[parsed] = (rubraview_zip_entry_t){
            .name = { .ptr = (const char*)(p + ZIP_CENTRAL_HEADER_MIN_SIZE), .len = filename_len },
            .uncompressed_size = uncompressed_size,
            .compressed_size = compressed_size,
            .compression_method = method,
            .local_header_offset = local_header_offset,
            .utf8_flag = (gp_flags & ZIP_GP_FLAG_UTF8) != 0,
        };

        p = record_end;
        parsed++;
    }

    result.err = RUBRAVIEW_ZIP_OK;
    result.value.data = data;
    result.value.size = size;
    result.value.entries = entries;
    result.value.entry_count = total_entries;
    return result;
}

rubraview_zip_data_result_t rubraview_zip_read_stored(const rubraview_zip_archive_t *zip, size_t entry_index, uint32_t max_uncompressed_bytes) {
    rubraview_zip_data_result_t result = {0};
    if (!zip || !zip->entries || entry_index >= zip->entry_count) {
        result.err = RUBRAVIEW_ZIP_ERR_BAD_INDEX;
        return result;
    }

    const rubraview_zip_entry_t *entry = &zip->entries[entry_index];

    if (entry->compression_method != ZIP_METHOD_STORED) {
        result.err = RUBRAVIEW_ZIP_ERR_UNSUPPORTED_COMPRESSION;
        return result;
    }
    if (entry->uncompressed_size > max_uncompressed_bytes) {
        result.err = RUBRAVIEW_ZIP_ERR_TOO_LARGE;
        return result;
    }

    if ((uint64_t)entry->local_header_offset + ZIP_LOCAL_HEADER_MIN_SIZE > (uint64_t)zip->size) {
        result.err = RUBRAVIEW_ZIP_ERR_TRUNCATED;
        return result;
    }

    const uint8_t *local = zip->data + entry->local_header_offset;
    if (rd_u32(local) != ZIP_SIG_LOCAL_HEADER) {
        result.err = RUBRAVIEW_ZIP_ERR_CORRUPT;
        return result;
    }

    uint16_t local_filename_len = rd_u16(local + 26);
    uint16_t local_extra_len = rd_u16(local + 28);

    uint64_t data_offset = (uint64_t)entry->local_header_offset + ZIP_LOCAL_HEADER_MIN_SIZE + local_filename_len + local_extra_len;
    uint64_t data_end = data_offset + entry->uncompressed_size;

    if (data_end > (uint64_t)zip->size) {
        result.err = RUBRAVIEW_ZIP_ERR_TRUNCATED;
        return result;
    }

    result.err = RUBRAVIEW_ZIP_OK;
    result.data.ptr = (const char*)(zip->data + data_offset);
    result.data.len = entry->uncompressed_size;
    return result;
}

/* Locates an entry's payload, whatever its compression method: the local
   header's own filename and extra lengths decide where the bytes start,
   and they may legitimately differ from the central directory's. */
static rubraview_zip_err_t entry_payload(const rubraview_zip_archive_t *zip,
                                         const rubraview_zip_entry_t *entry,
                                         const uint8_t **out_data,
                                         uint32_t *out_size) {
    if ((uint64_t)entry->local_header_offset + ZIP_LOCAL_HEADER_MIN_SIZE > (uint64_t)zip->size) {
        return RUBRAVIEW_ZIP_ERR_TRUNCATED;
    }

    const uint8_t *local = zip->data + entry->local_header_offset;
    if (rd_u32(local) != ZIP_SIG_LOCAL_HEADER) return RUBRAVIEW_ZIP_ERR_CORRUPT;

    uint16_t local_filename_len = rd_u16(local + 26);
    uint16_t local_extra_len = rd_u16(local + 28);

    uint64_t data_offset = (uint64_t)entry->local_header_offset + ZIP_LOCAL_HEADER_MIN_SIZE
                         + local_filename_len + local_extra_len;
    if (data_offset + entry->compressed_size > (uint64_t)zip->size) {
        return RUBRAVIEW_ZIP_ERR_TRUNCATED;
    }

    *out_data = zip->data + data_offset;
    *out_size = entry->compressed_size;
    return RUBRAVIEW_ZIP_OK;
}

rubraview_zip_data_result_t rubraview_zip_read_entry(proven_arena_t *arena,
                                                     const rubraview_zip_archive_t *zip,
                                                     size_t entry_index,
                                                     uint32_t max_uncompressed_bytes) {
    rubraview_zip_data_result_t result = {0};
    if (!zip || !zip->entries || entry_index >= zip->entry_count) {
        result.err = RUBRAVIEW_ZIP_ERR_BAD_INDEX;
        return result;
    }

    const rubraview_zip_entry_t *entry = &zip->entries[entry_index];

    /* §10.2: the declared size is checked before a single byte is
       committed, so an archive claiming a gigabyte costs nothing. */
    if (entry->uncompressed_size > max_uncompressed_bytes) {
        result.err = RUBRAVIEW_ZIP_ERR_TOO_LARGE;
        return result;
    }

    if (entry->compression_method == ZIP_METHOD_STORED) {
        return rubraview_zip_read_stored(zip, entry_index, max_uncompressed_bytes);
    }
    if (entry->compression_method != ZIP_METHOD_DEFLATE) {
        result.err = RUBRAVIEW_ZIP_ERR_UNSUPPORTED_COMPRESSION;
        return result;
    }
    if (!arena) {
        result.err = RUBRAVIEW_ZIP_ERR_OUT_OF_MEMORY;
        return result;
    }

    const uint8_t *compressed = NULL;
    uint32_t compressed_size = 0;
    rubraview_zip_err_t located = entry_payload(zip, entry, &compressed, &compressed_size);
    if (located != RUBRAVIEW_ZIP_OK) {
        result.err = located;
        return result;
    }

    if (entry->uncompressed_size == 0) {
        result.err = RUBRAVIEW_ZIP_OK;
        result.data = (u8str_t){ .ptr = "", .len = 0 };
        return result;
    }

    proven_result_mem_mut_t res = proven_arena_alloc(arena, (size_t)entry->uncompressed_size + 1);
    if (!proven_is_ok(res.err)) {
        result.err = RUBRAVIEW_ZIP_ERR_OUT_OF_MEMORY;
        return result;
    }

    /* Raw DEFLATE: a ZIP entry carries no zlib header, and the output is
       bounded by the declared size, so a stream that tries to expand
       past it simply fails rather than growing the buffer. */
    size_t produced = tinfl_decompress_mem_to_mem(res.value.ptr, (size_t)entry->uncompressed_size,
                                                  compressed, (size_t)compressed_size, 0);
    if (produced != (size_t)entry->uncompressed_size) {
        result.err = RUBRAVIEW_ZIP_ERR_CORRUPT_STREAM;
        return result;
    }

    res.value.ptr[entry->uncompressed_size] = '\0';
    result.err = RUBRAVIEW_ZIP_OK;
    result.data = (u8str_t){ .ptr = (const char*)res.value.ptr, .len = (size_t)entry->uncompressed_size };
    return result;
}
