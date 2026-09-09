#include "rubraview/pagesource.h"
#include "rubraview/comicinfo.h"
#include "miniz.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

static u8str_t lit(const char *s) {
    return (u8str_t){ .ptr = s, .len = strlen(s) };
}

static bool str_eq(u8str_t s, const char *l) {
    size_t n = strlen(l);
    return s.len == n && (n == 0 || memcmp(s.ptr, l, n) == 0);
}

static void put_u16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v & 0xFF); p[1] = (uint8_t)(v >> 8); }
static void put_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFF); p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF); p[3] = (uint8_t)((v >> 24) & 0xFF);
}

typedef struct { const char *name; const char *content; bool deflate; } zip_entry_t;

/* A minimal CBZ builder, enough to exercise the reader under test. */
static size_t build_zip(uint8_t *out, const zip_entry_t *entries, size_t count) {
    size_t pos = 0;
    uint32_t offsets[16];
    uint32_t sizes[16];
    assert(count <= 16);

    for (size_t i = 0; i < count; ++i) {
        offsets[i] = (uint32_t)pos;
        size_t name_len = strlen(entries[i].name);
        size_t content_len = strlen(entries[i].content);

        unsigned char deflated[8192];
        size_t stored_len = content_len;
        if (entries[i].deflate) {
            mz_stream stream = {0};
            stream.next_in = (const unsigned char*)entries[i].content;
            stream.avail_in = (unsigned int)content_len;
            stream.next_out = deflated;
            stream.avail_out = (unsigned int)sizeof(deflated);
            assert(mz_deflateInit2(&stream, MZ_DEFAULT_COMPRESSION, MZ_DEFLATED,
                                   -MZ_DEFAULT_WINDOW_BITS, 9, MZ_DEFAULT_STRATEGY) == MZ_OK);
            assert(mz_deflate(&stream, MZ_FINISH) == MZ_STREAM_END);
            stored_len = stream.total_out;
            mz_deflateEnd(&stream);
        }
        sizes[i] = (uint32_t)stored_len;

        put_u32(out + pos, 0x04034b50u); pos += 4;
        put_u16(out + pos, 20); pos += 2;
        put_u16(out + pos, 0x0800); pos += 2; /* UTF-8 flag */
        put_u16(out + pos, entries[i].deflate ? 8 : 0); pos += 2;
        put_u16(out + pos, 0); pos += 2;
        put_u16(out + pos, 0); pos += 2;
        put_u32(out + pos, 0); pos += 4;
        put_u32(out + pos, (uint32_t)stored_len); pos += 4;
        put_u32(out + pos, (uint32_t)content_len); pos += 4;
        put_u16(out + pos, (uint16_t)name_len); pos += 2;
        put_u16(out + pos, 0); pos += 2;
        memcpy(out + pos, entries[i].name, name_len); pos += name_len;
        if (entries[i].deflate) { memcpy(out + pos, deflated, stored_len); }
        else { memcpy(out + pos, entries[i].content, content_len); }
        pos += stored_len;
    }

    size_t cd_start = pos;
    for (size_t i = 0; i < count; ++i) {
        size_t name_len = strlen(entries[i].name);
        size_t content_len = strlen(entries[i].content);

        put_u32(out + pos, 0x02014b50u); pos += 4;
        put_u16(out + pos, 20); pos += 2;
        put_u16(out + pos, 20); pos += 2;
        put_u16(out + pos, 0x0800); pos += 2;
        put_u16(out + pos, entries[i].deflate ? 8 : 0); pos += 2;
        put_u16(out + pos, 0); pos += 2;
        put_u16(out + pos, 0); pos += 2;
        put_u32(out + pos, 0); pos += 4;
        put_u32(out + pos, sizes[i]); pos += 4;
        put_u32(out + pos, (uint32_t)content_len); pos += 4;
        put_u16(out + pos, (uint16_t)name_len); pos += 2;
        put_u16(out + pos, 0); pos += 2;
        put_u16(out + pos, 0); pos += 2;
        put_u16(out + pos, 0); pos += 2;
        put_u16(out + pos, 0); pos += 2;
        put_u32(out + pos, 0); pos += 4;
        put_u32(out + pos, offsets[i]); pos += 4;
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

#include "fixtures_7z.inc"

int main(void) {
    printf("[test_pagesource] Starting page source (folder and CBZ) unit tests...\n");

    size_t mem_size = 1024 * 1024;
    void *raw = malloc(mem_size);
    assert(raw != NULL);
    proven_arena_t arena = proven_arena_create((proven_mem_mut_t){ .ptr = raw, .size = mem_size });

    /* Test 1: A CBZ becomes an ordered run of pages, naturally sorted,
       with non-images and directory entries left out (§3.8.1). */
    {
        uint8_t zip[16384];
        zip_entry_t entries[] = {
            { "pages/", "", false },
            { "page (10).jpg", "TEN", true },
            { "page (2).jpg", "TWO", true },
            { "notes.txt", "not an image", false },
            { "page (1).jpg", "ONE", false },
        };
        size_t len = build_zip(zip, entries, 5);

        rubraview_page_source_t source = rubraview_page_source_from_archive(
            &arena, zip, len, lit("D:/Comics/Vol01.cbz"), U8("*.jpg;*.png"),
            RUBRAVIEW_CODEPAGE_AUTO, UINT32_MAX, UINT64_MAX);

        assert(source.kind == RUBRAVIEW_PAGE_SOURCE_ARCHIVE);
        assert(source.page_count == 3); /* the directory entry and the text file are not pages */
        assert(str_eq(source.pages[0].name, "page (1).jpg"));
        assert(str_eq(source.pages[1].name, "page (2).jpg"));
        assert(str_eq(source.pages[2].name, "page (10).jpg")); /* natural, not lexical */
    }
    printf("  [PASS] A CBZ lists only image pages, in natural order\n");

    /* Test 2: Reading a page yields its bytes from memory, never from
       disk — the zero-disk invariant (§3.8.1). */
    {
        uint8_t zip[16384];
        zip_entry_t entries[] = {
            { "p1.jpg", "FIRST PAGE FIRST PAGE FIRST PAGE FIRST PAGE", true },
            { "p2.jpg", "SECOND", false },
        };
        size_t len = build_zip(zip, entries, 2);

        rubraview_page_source_t source = rubraview_page_source_from_archive(
            &arena, zip, len, lit("a.cbz"), U8("*.jpg"), RUBRAVIEW_CODEPAGE_AUTO, UINT32_MAX, UINT64_MAX);
        assert(source.page_count == 2);

        rubraview_page_bytes_t first = rubraview_page_source_read(&arena, &source, 0, UINT32_MAX);
        assert(first.ok && !first.from_disk);
        assert(str_eq(first.data, "FIRST PAGE FIRST PAGE FIRST PAGE FIRST PAGE"));

        rubraview_page_bytes_t second = rubraview_page_source_read(&arena, &source, 1, UINT32_MAX);
        assert(second.ok && !second.from_disk);
        assert(str_eq(second.data, "SECOND"));
    }
    printf("  [PASS] Archive pages are produced from memory, deflated or stored\n");

    /* Test 3: §3.8.5 — ComicInfo.xml is picked out of the archive rather
       than shown as a page, and drives the layout. */
    {
        uint8_t zip[16384];
        zip_entry_t entries[] = {
            { "ComicInfo.xml", "<ComicInfo><Manga>YesAndRightToLeft</Manga>"
                               "<Pages><Page Image=\"0\" Type=\"FrontCover\" /></Pages></ComicInfo>", true },
            { "001.jpg", "A", false },
            { "002.jpg", "B", false },
        };
        size_t len = build_zip(zip, entries, 3);

        rubraview_page_source_t source = rubraview_page_source_from_archive(
            &arena, zip, len, lit("m.cbz"), U8("*.jpg"), RUBRAVIEW_CODEPAGE_AUTO, UINT32_MAX, UINT64_MAX);

        assert(source.page_count == 2); /* the manifest is not a page */
        assert(source.has_comicinfo);

        rubraview_comicinfo_t info = rubraview_comicinfo_parse(&arena, source.comicinfo_xml);
        assert(info.manga == RUBRAVIEW_MANGA_YES_RTL);

        rubraview_layout_opts_t opts = rubraview_layout_opts_default(RUBRAVIEW_PAGE_LAYOUT_SINGLE, RUBRAVIEW_READING_LTR);
        rubraview_page_info_t pages[2] = { { .width = 800, .height = 1200 }, { .width = 800, .height = 1200 } };
        rubraview_comicinfo_apply(&info, &opts, pages, 2);

        assert(opts.direction == RUBRAVIEW_READING_RTL);
        assert(pages[0].force_standalone);
    }
    printf("  [PASS] ComicInfo.xml is extracted, not listed, and configures the layout\n");

    /* Test 4: A folder listing becomes the same kind of source, and its
       pages are opened from disk rather than decompressed. */
    {
        rubraview_fs_entry_t entries[3] = {
            { .name = lit("b (2).png"), .path = lit("/d/b (2).png") },
            { .name = lit("b (10).png"), .path = lit("/d/b (10).png") },
            { .name = lit("b (1).png"), .path = lit("/d/b (1).png") },
        };
        rubraview_fs_listing_t listing = { .entries = entries, .count = 3 };

        rubraview_page_source_t source = rubraview_page_source_from_listing(
            &arena, &listing, U8("*.png"), RUBRAVIEW_SORT_NAME_NATURAL, true);

        assert(source.kind == RUBRAVIEW_PAGE_SOURCE_FOLDER);
        assert(source.page_count == 3);
        assert(str_eq(source.pages[0].name, "b (1).png"));
        assert(str_eq(source.pages[2].name, "b (10).png"));

        rubraview_page_bytes_t bytes = rubraview_page_source_read(&arena, &source, 0, UINT32_MAX);
        assert(bytes.ok && bytes.from_disk);
        assert(bytes.data.len == 0); /* the caller opens the path itself */
    }
    printf("  [PASS] A folder is the same kind of source, opened from disk\n");

    /* Test 5: §3.8.1 point 4 — the next and previous archive in the same
       directory, so one volume runs into the next. */
    {
        rubraview_fs_entry_t entries[4] = {
            { .name = lit("Vol 02.cbz"), .path = lit("/c/Vol 02.cbz") },
            { .name = lit("Vol 10.cbz"), .path = lit("/c/Vol 10.cbz") },
            { .name = lit("Vol 01.cbz"), .path = lit("/c/Vol 01.cbz") },
            { .name = lit("cover.jpg"),  .path = lit("/c/cover.jpg") },
        };
        rubraview_fs_listing_t listing = { .entries = entries, .count = 4 };

        u8str_t next = rubraview_page_source_sibling_archive(&arena, &listing, lit("/c/Vol 01.cbz"), true);
        assert(str_eq(next, "/c/Vol 02.cbz"));

        u8str_t after = rubraview_page_source_sibling_archive(&arena, &listing, lit("/c/Vol 02.cbz"), true);
        assert(str_eq(after, "/c/Vol 10.cbz")); /* natural order: 02 then 10 */

        u8str_t back = rubraview_page_source_sibling_archive(&arena, &listing, lit("/c/Vol 02.cbz"), false);
        assert(str_eq(back, "/c/Vol 01.cbz"));

        /* Past the ends there is nothing, and a file that is not an
           archive in this directory has no siblings to step through. */
        assert(rubraview_page_source_sibling_archive(&arena, &listing, lit("/c/Vol 10.cbz"), true).len == 0);
        assert(rubraview_page_source_sibling_archive(&arena, &listing, lit("/c/Vol 01.cbz"), false).len == 0);
        assert(rubraview_page_source_sibling_archive(&arena, &listing, lit("/c/cover.jpg"), true).len == 0);
    }
    printf("  [PASS] Consecutive archive traversal steps volume to volume in natural order\n");

    /* Test 6: A buffer that is not an archive yields an empty source
       rather than a crash, and out-of-range reads are refused. */
    {
        uint8_t garbage[128];
        memset(garbage, 0x5A, sizeof(garbage));
        rubraview_page_source_t source = rubraview_page_source_from_archive(
            &arena, garbage, sizeof(garbage), lit("x.cbz"), U8("*.jpg"),
            RUBRAVIEW_CODEPAGE_AUTO, UINT32_MAX, UINT64_MAX);
        assert(source.page_count == 0);

        rubraview_page_bytes_t bytes = rubraview_page_source_read(&arena, &source, 0, UINT32_MAX);
        assert(!bytes.ok);
    }
    printf("  [PASS] A non-archive buffer yields an empty source, reads refused\n");

    /* Test 7: §10.2 — the per-page size cap reaches through the page
       source, so a bomb inside a CBZ is refused at the page level too. */
    {
        uint8_t zip[16384];
        zip_entry_t entries[] = { { "big.jpg", "0123456789ABCDEF0123456789ABCDEF", true } };
        size_t len = build_zip(zip, entries, 1);

        rubraview_page_source_t source = rubraview_page_source_from_archive(
            &arena, zip, len, lit("b.cbz"), U8("*.jpg"), RUBRAVIEW_CODEPAGE_AUTO, UINT32_MAX, UINT64_MAX);
        assert(source.page_count == 1);

        rubraview_page_bytes_t capped = rubraview_page_source_read(&arena, &source, 0, 8);
        assert(!capped.ok);
    }
    printf("  [PASS] The size cap applies through the page source as well\n");

    /* Test: a CB7 goes down the 7z path, and the caller cannot tell.
       §3.8.2 — recognised by signature, not by the name it was given. */
    {
        rubraview_page_source_t source = rubraview_page_source_from_archive(
            &arena, SZ_SOLID, sizeof(SZ_SOLID), lit("D:/Comics/Vol01.cb7"), U8("*.txt"),
            RUBRAVIEW_CODEPAGE_AUTO, UINT32_MAX, UINT64_MAX);

        assert(source.kind == RUBRAVIEW_PAGE_SOURCE_ARCHIVE_7Z);
        assert(source.page_count == 2);
        assert(str_eq(source.pages[0].name, "001.txt"));
        assert(source.has_comicinfo); /* ComicInfo.xml is extracted, not listed as a page */

        rubraview_page_bytes_t first = rubraview_page_source_read(&arena, &source, 0, UINT32_MAX);
        assert(first.ok && !first.from_disk);
        assert(first.data.len == 35);

        rubraview_page_source_close(&source);
    }
    printf("  [PASS] A CB7 is recognised by signature and read through the same interface\n");

    /* Test: an archive named .cbz that is really a 7z still opens —
       the extension is a hint, the signature is the evidence. */
    {
        rubraview_page_source_t source = rubraview_page_source_from_archive(
            &arena, SZ_NONSOLID, sizeof(SZ_NONSOLID), lit("mislabelled.cbz"), U8("*.txt"),
            RUBRAVIEW_CODEPAGE_AUTO, UINT32_MAX, UINT64_MAX);
        assert(source.kind == RUBRAVIEW_PAGE_SOURCE_ARCHIVE_7Z);
        assert(source.page_count == 2);
        rubraview_page_source_close(&source);
    }
    printf("  [PASS] A 7z with a .cbz name is still read as a 7z\n");

    free(raw);
    printf("[test_pagesource] All tests passed successfully!\n");
    return 0;
}
