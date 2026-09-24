#include "rubraview/archive.h"
#include "miniz.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

static void put_u16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v & 0xFF); p[1] = (uint8_t)(v >> 8); }
static void put_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFF); p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF); p[3] = (uint8_t)((v >> 24) & 0xFF);
}

typedef struct { const char *name; const char *content; bool utf8_flag; bool deflate; } test_zip_entry_t;

/* Builds a minimal, valid STORED-only ZIP into `out`, returning its total
   byte length. This is a from-scratch encoder written only to exercise
   the reader under test — it is not a general ZIP writer. */
static size_t build_zip(uint8_t *out, size_t out_cap, const test_zip_entry_t *entries, size_t count) {
    (void)out_cap;
    size_t pos = 0;
    uint32_t local_offsets[8];
    assert(count <= 8);

    /* Compressed sizes are recorded as each entry is written so the
       central directory can repeat them. */
    static uint32_t compressed_sizes[8];

    for (size_t i = 0; i < count; ++i) {
        local_offsets[i] = (uint32_t)pos;
        size_t name_len = strlen(entries[i].name);
        size_t content_len = strlen(entries[i].content);

        /* Deflate the payload with miniz's raw (headerless) stream, which
           is what a ZIP entry carries. */
        unsigned char deflated[4096];
        size_t deflated_len = 0;
        if (entries[i].deflate) {
            mz_stream stream = {0};
            stream.next_in = (const unsigned char*)entries[i].content;
            stream.avail_in = (unsigned int)content_len;
            stream.next_out = deflated;
            stream.avail_out = (unsigned int)sizeof(deflated);
            int init = mz_deflateInit2(&stream, MZ_DEFAULT_COMPRESSION, MZ_DEFLATED,
                                       -MZ_DEFAULT_WINDOW_BITS, 9, MZ_DEFAULT_STRATEGY);
            assert(init == MZ_OK);
            int done = mz_deflate(&stream, MZ_FINISH);
            assert(done == MZ_STREAM_END);
            deflated_len = stream.total_out;
            mz_deflateEnd(&stream);
        }

        size_t stored_len = entries[i].deflate ? deflated_len : content_len;
        compressed_sizes[i] = (uint32_t)stored_len;

        put_u32(out + pos, 0x04034b50u); pos += 4;
        put_u16(out + pos, 20); pos += 2;
        put_u16(out + pos, entries[i].utf8_flag ? 0x0800 : 0); pos += 2;
        put_u16(out + pos, entries[i].deflate ? 8 : 0); pos += 2; /* method */
        put_u16(out + pos, 0); pos += 2;
        put_u16(out + pos, 0); pos += 2;
        put_u32(out + pos, 0); pos += 4; /* crc32, unchecked by the reader */
        put_u32(out + pos, (uint32_t)stored_len); pos += 4;
        put_u32(out + pos, (uint32_t)content_len); pos += 4;
        put_u16(out + pos, (uint16_t)name_len); pos += 2;
        put_u16(out + pos, 0); pos += 2;
        memcpy(out + pos, entries[i].name, name_len); pos += name_len;
        if (entries[i].deflate) {
            memcpy(out + pos, deflated, deflated_len); pos += deflated_len;
        } else {
            memcpy(out + pos, entries[i].content, content_len); pos += content_len;
        }
    }

    size_t cd_start = pos;
    for (size_t i = 0; i < count; ++i) {
        size_t name_len = strlen(entries[i].name);
        size_t content_len = strlen(entries[i].content);

        put_u32(out + pos, 0x02014b50u); pos += 4;
        put_u16(out + pos, 20); pos += 2;
        put_u16(out + pos, 20); pos += 2;
        put_u16(out + pos, entries[i].utf8_flag ? 0x0800 : 0); pos += 2;
        put_u16(out + pos, entries[i].deflate ? 8 : 0); pos += 2; /* method */
        put_u16(out + pos, 0); pos += 2;
        put_u16(out + pos, 0); pos += 2;
        put_u32(out + pos, 0); pos += 4;
        put_u32(out + pos, compressed_sizes[i]); pos += 4;
        put_u32(out + pos, (uint32_t)content_len); pos += 4;
        put_u16(out + pos, (uint16_t)name_len); pos += 2;
        put_u16(out + pos, 0); pos += 2;
        put_u16(out + pos, 0); pos += 2;
        put_u16(out + pos, 0); pos += 2;
        put_u16(out + pos, 0); pos += 2;
        put_u32(out + pos, 0); pos += 4;
        put_u32(out + pos, local_offsets[i]); pos += 4;
        memcpy(out + pos, entries[i].name, name_len); pos += name_len;
    }
    size_t cd_size = pos - cd_start;

    put_u32(out + pos, 0x06054b50u); pos += 4;
    put_u16(out + pos, 0); pos += 2;
    put_u16(out + pos, 0); pos += 2;
    put_u16(out + pos, (uint16_t)count); pos += 2;
    put_u16(out + pos, (uint16_t)count); pos += 2;
    put_u32(out + pos, (uint32_t)cd_size); pos += 4;
    put_u32(out + pos, (uint32_t)cd_start); pos += 4;
    put_u16(out + pos, 0); pos += 2;

    return pos;
}

static bool str_eq(u8str_t s, const char *l) {
    size_t n = strlen(l);
    return s.len == n && (n == 0 || memcmp(s.ptr, l, n) == 0);
}

int main(void) {
    printf("[test_archive] Starting ZIP archive index/streaming unit tests...\n");

    size_t mem_size = 64 * 1024;
    void *raw_mem = malloc(mem_size);
    assert(raw_mem != NULL);
    proven_arena_t arena = proven_arena_create((proven_mem_mut_t){ .ptr = raw_mem, .size = mem_size });

    /* Test 1: Two stored entries, mixed UTF-8 flag, round-trip read. */
    {
        uint8_t zip_buf[4096];
        test_zip_entry_t src[] = {
            { .name = "page1.txt", .content = "Hello", .utf8_flag = true },
            { .name = "page2.txt", .content = "World!!", .utf8_flag = false },
        };
        size_t zip_len = build_zip(zip_buf, sizeof(zip_buf), src, 2);

        rubraview_zip_result_t r = rubraview_zip_open(&arena, zip_buf, zip_len);
        assert(r.err == RUBRAVIEW_ZIP_OK);
        assert(r.value.entry_count == 2);
        assert(str_eq(r.value.entries[0].name, "page1.txt"));
        assert(r.value.entries[0].utf8_flag == true);
        assert(str_eq(r.value.entries[1].name, "page2.txt"));
        assert(r.value.entries[1].utf8_flag == false);

        rubraview_zip_data_result_t d0 = rubraview_zip_read_stored(&r.value, 0, UINT32_MAX);
        assert(d0.err == RUBRAVIEW_ZIP_OK);
        assert(str_eq(d0.data, "Hello"));

        rubraview_zip_data_result_t d1 = rubraview_zip_read_stored(&r.value, 1, UINT32_MAX);
        assert(d1.err == RUBRAVIEW_ZIP_OK);
        assert(str_eq(d1.data, "World!!"));
    }
    printf("  [PASS] Two stored entries index and read back correctly, UTF-8 flag preserved\n");

    /* Test 2: Zero-copy — the returned view points into the caller's buffer. */
    {
        uint8_t zip_buf[4096];
        test_zip_entry_t src[] = { { .name = "a.png", .content = "PNGDATA", .utf8_flag = false } };
        size_t zip_len = build_zip(zip_buf, sizeof(zip_buf), src, 1);

        rubraview_zip_result_t r = rubraview_zip_open(&arena, zip_buf, zip_len);
        rubraview_zip_data_result_t d = rubraview_zip_read_stored(&r.value, 0, UINT32_MAX);
        assert(d.err == RUBRAVIEW_ZIP_OK);
        assert(d.data.ptr >= (const char*)zip_buf && d.data.ptr < (const char*)zip_buf + zip_len);
    }
    printf("  [PASS] Stored entry read is a zero-copy view into the source buffer\n");

    /* Test 3: A non-ZIP buffer is rejected. */
    {
        uint8_t garbage[64];
        memset(garbage, 0x41, sizeof(garbage));
        rubraview_zip_result_t r = rubraview_zip_open(&arena, garbage, sizeof(garbage));
        assert(r.err == RUBRAVIEW_ZIP_ERR_NOT_A_ZIP);
    }
    printf("  [PASS] Non-ZIP buffer rejected as RUBRAVIEW_ZIP_ERR_NOT_A_ZIP\n");

    /* Test 4: A buffer truncated mid-Central-Directory is rejected. */
    {
        uint8_t zip_buf[4096];
        test_zip_entry_t src[] = {
            { .name = "page1.txt", .content = "Hello", .utf8_flag = false },
            { .name = "page2.txt", .content = "World", .utf8_flag = false },
        };
        size_t zip_len = build_zip(zip_buf, sizeof(zip_buf), src, 2);

        /* Truncate 10 bytes off the end: the EOCD's own signature moves out
           of range, so this must present as NOT_A_ZIP (no EOCD found) —
           exercising a different failure than a truncated CD payload. */
        rubraview_zip_result_t r1 = rubraview_zip_open(&arena, zip_buf, zip_len - 10);
        assert(r1.err == RUBRAVIEW_ZIP_ERR_NOT_A_ZIP);

        /* Keep the EOCD intact (last 22 bytes) but cut into the CD payload
           it claims precedes it: the CD offset/size no longer fit before
           the EOCD, which must be caught. */
        uint8_t patched[4096];
        memcpy(patched, zip_buf, zip_len);
        size_t eocd_start = zip_len - 22;
        size_t shrink = 5;
        memmove(patched + eocd_start - shrink, patched + eocd_start, 22);
        rubraview_zip_result_t r2 = rubraview_zip_open(&arena, patched, zip_len - shrink);
        assert(r2.err == RUBRAVIEW_ZIP_ERR_TRUNCATED);
    }
    printf("  [PASS] Truncated buffers rejected (missing EOCD, and CD past EOCD)\n");

    /* Test 5: A deflate-method entry is indexed (open succeeds) but
       rejected at read time as unsupported (RV-051/D-2 not yet vendored). */
    {
        uint8_t zip_buf[4096];
        test_zip_entry_t src[] = { { .name = "compressed.jpg", .content = "irrelevant", .utf8_flag = false } };
        size_t zip_len = build_zip(zip_buf, sizeof(zip_buf), src, 1);

        /* Patch method=8 (deflate) into both the local header (offset 8)
           and the Central Directory header (offset local_cd_method). */
        put_u16(zip_buf + 8, 8);
        /* Central directory record starts right after the local entry:
           local header (30) + name(14) + content(10) = 54. */
        size_t cd_record_start = 30 + strlen("compressed.jpg") + strlen("irrelevant");
        put_u16(zip_buf + cd_record_start + 10, 8);

        rubraview_zip_result_t r = rubraview_zip_open(&arena, zip_buf, zip_len);
        assert(r.err == RUBRAVIEW_ZIP_OK);
        assert(r.value.entries[0].compression_method == 8);

        rubraview_zip_data_result_t d = rubraview_zip_read_stored(&r.value, 0, UINT32_MAX);
        assert(d.err == RUBRAVIEW_ZIP_ERR_UNSUPPORTED_COMPRESSION);
    }
    printf("  [PASS] Deflate entries index cleanly but are rejected at read time\n");

    /* Test 6: The §10.2 per-entry size cap rejects an oversized entry. */
    {
        uint8_t zip_buf[4096];
        test_zip_entry_t src[] = { { .name = "huge.png", .content = "0123456789", .utf8_flag = false } };
        size_t zip_len = build_zip(zip_buf, sizeof(zip_buf), src, 1);

        rubraview_zip_result_t r = rubraview_zip_open(&arena, zip_buf, zip_len);
        assert(r.err == RUBRAVIEW_ZIP_OK);

        rubraview_zip_data_result_t d = rubraview_zip_read_stored(&r.value, 0, 5 /* smaller than the 10-byte entry */);
        assert(d.err == RUBRAVIEW_ZIP_ERR_TOO_LARGE);
    }
    printf("  [PASS] Oversized entry rejected against the caller's size cap\n");

    /* Test 7: An out-of-range entry index is rejected. */
    {
        uint8_t zip_buf[4096];
        test_zip_entry_t src[] = { { .name = "only.txt", .content = "x", .utf8_flag = false } };
        size_t zip_len = build_zip(zip_buf, sizeof(zip_buf), src, 1);
        rubraview_zip_result_t r = rubraview_zip_open(&arena, zip_buf, zip_len);
        rubraview_zip_data_result_t d = rubraview_zip_read_stored(&r.value, 5, UINT32_MAX);
        assert(d.err == RUBRAVIEW_ZIP_ERR_BAD_INDEX);
    }
    printf("  [PASS] Out-of-range entry index rejected as RUBRAVIEW_ZIP_ERR_BAD_INDEX\n");

    /* Test 8: An empty archive (zero entries) opens cleanly. */
    {
        uint8_t zip_buf[64];
        size_t zip_len = build_zip(zip_buf, sizeof(zip_buf), NULL, 0);
        rubraview_zip_result_t r = rubraview_zip_open(&arena, zip_buf, zip_len);
        assert(r.err == RUBRAVIEW_ZIP_OK);
        assert(r.value.entry_count == 0);
    }
    printf("  [PASS] Empty archive (zero entries) opens cleanly\n");

    /* Test 9: A DEFLATE entry inflates back to its exact contents
       (RV-051, owner decision D-2). The payload is deliberately
       repetitive so it really does compress. */
    {
        uint8_t zip_buf[8192];
        const char *page_text =
            "PAGE DATA PAGE DATA PAGE DATA PAGE DATA PAGE DATA PAGE DATA "
            "PAGE DATA PAGE DATA PAGE DATA PAGE DATA PAGE DATA PAGE DATA";
        test_zip_entry_t src[] = {
            { .name = "page01.jpg", .content = page_text, .utf8_flag = true, .deflate = true },
            { .name = "page02.jpg", .content = "stored page", .utf8_flag = true, .deflate = false },
        };
        size_t zip_len = build_zip(zip_buf, sizeof(zip_buf), src, 2);

        rubraview_zip_result_t r = rubraview_zip_open(&arena, zip_buf, zip_len);
        assert(r.err == RUBRAVIEW_ZIP_OK);
        assert(r.value.entries[0].compression_method == 8);
        assert(r.value.entries[0].compressed_size < r.value.entries[0].uncompressed_size);

        rubraview_zip_data_result_t inflated = rubraview_zip_read_entry(&arena, &r.value, 0, UINT32_MAX);
        assert(inflated.err == RUBRAVIEW_ZIP_OK);
        assert(str_eq(inflated.data, page_text));

        /* The same call still returns a zero-copy view for a stored entry. */
        rubraview_zip_data_result_t stored = rubraview_zip_read_entry(&arena, &r.value, 1, UINT32_MAX);
        assert(stored.err == RUBRAVIEW_ZIP_OK);
        assert(str_eq(stored.data, "stored page"));
        assert(stored.data.ptr >= (const char*)zip_buf && stored.data.ptr < (const char*)zip_buf + zip_len);
    }
    printf("  [PASS] DEFLATE entries inflate exactly; stored entries stay zero-copy\n");

    /* Test 10: §10.2 — the size cap is applied to a compressed entry
       before anything is allocated, so a small archive claiming a huge
       payload costs nothing. */
    {
        uint8_t zip_buf[8192];
        test_zip_entry_t src[] = {
            { .name = "big.jpg", .content = "0123456789ABCDEF", .utf8_flag = false, .deflate = true },
        };
        size_t zip_len = build_zip(zip_buf, sizeof(zip_buf), src, 1);
        rubraview_zip_result_t r = rubraview_zip_open(&arena, zip_buf, zip_len);

        rubraview_zip_data_result_t capped = rubraview_zip_read_entry(&arena, &r.value, 0, 4);
        assert(capped.err == RUBRAVIEW_ZIP_ERR_TOO_LARGE);
    }
    printf("  [PASS] Size cap rejects an oversized compressed entry before allocating\n");

    /* Test 11: A corrupted compressed stream is reported, not accepted.
       The reader must never hand back a half-inflated page. */
    {
        uint8_t zip_buf[8192];
        const char *text = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";
        test_zip_entry_t src[] = {
            { .name = "broken.jpg", .content = text, .utf8_flag = false, .deflate = true },
        };
        size_t zip_len = build_zip(zip_buf, sizeof(zip_buf), src, 1);

        /* Corrupt a byte inside the compressed payload: it sits after the
           30-byte local header and the 10-character name. */
        size_t payload = 30 + strlen("broken.jpg");
        zip_buf[payload + 2] ^= 0xFF;

        rubraview_zip_result_t r = rubraview_zip_open(&arena, zip_buf, zip_len);
        assert(r.err == RUBRAVIEW_ZIP_OK);
        rubraview_zip_data_result_t broken = rubraview_zip_read_entry(&arena, &r.value, 0, UINT32_MAX);
        assert(broken.err == RUBRAVIEW_ZIP_ERR_CORRUPT_STREAM);
    }
    printf("  [PASS] A corrupted deflate stream is rejected, never half-inflated\n");

    /* Reading a ZIP a piece at a time (D-34): the directory found from the
       file's tail alone, then one entry copied into an otherwise zeroed
       buffer of the file's size reads back whole; an entry that was never
       read fails cleanly instead of returning zeros as data. */
    {
        uint8_t zip_buf[4096];
        test_zip_entry_t src[] = {
            { .name = "a.txt", .content = "first entry" },
            { .name = "b.txt", .content = "second one" },
            { .name = "c.txt", .content = "third" },
        };
        size_t zip_len = build_zip(zip_buf, sizeof(zip_buf), src, 3);
        size_t tail = 60;   /* less than the file: the EOCD and part of the directory */
        uint64_t cd_off = 0, cd_size = 0;
        assert(!rubraview_zip_locate_directory(zip_buf + zip_len - 10, 10, zip_len, &cd_off, &cd_size));
        assert(rubraview_zip_locate_directory(zip_buf + zip_len - tail, tail, zip_len, &cd_off, &cd_size));
        assert(cd_off > 0 && cd_off + cd_size < zip_len);

        static uint8_t sparse[4096];
        memset(sparse, 0, sizeof(sparse));
        memcpy(sparse + cd_off, zip_buf + cd_off, zip_len - cd_off);   /* directory and EOCD */
        rubraview_zip_result_t r = rubraview_zip_open(&arena, sparse, zip_len);
        assert(r.err == RUBRAVIEW_ZIP_OK && r.value.entry_count == 3);

        const rubraview_zip_entry_t *b = &r.value.entries[1];
        uint64_t span = 0;
        assert(!rubraview_zip_local_span(sparse + b->local_header_offset, b->compressed_size, &span));   /* not read yet */
        assert(rubraview_zip_local_span(zip_buf + b->local_header_offset, b->compressed_size, &span));
        assert(span == 30 + 5 + 10);
        memcpy(sparse + b->local_header_offset, zip_buf + b->local_header_offset, (size_t)span);

        rubraview_zip_data_result_t got = rubraview_zip_read_entry(&arena, &r.value, 1, UINT32_MAX);
        assert(got.err == RUBRAVIEW_ZIP_OK && str_eq(got.data, "second one"));
        rubraview_zip_data_result_t missing = rubraview_zip_read_entry(&arena, &r.value, 0, UINT32_MAX);
        assert(missing.err != RUBRAVIEW_ZIP_OK);
    }
    printf("  [PASS] A ZIP read in pieces: the directory from its tail, one entry, the rest refused\n");

    free(raw_mem);
    printf("[test_archive] All tests passed successfully!\n");
    return 0;
}
