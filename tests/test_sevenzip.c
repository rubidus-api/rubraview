#include "rubraview/sevenzip.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

/*
 * CB7 reading (RV-052, §3.8.2). The archives below are real 7z files
 * produced by 7-Zip itself and pasted in as bytes: a decoder is only
 * proven by input it did not generate. `solid` puts all three files in
 * one block, which is the case the persistent decoder exists for;
 * `nonsolid` gives each file its own block; `encrypted` is the one that
 * must be refused rather than crashed on, since AES is deliberately not
 * vendored.
 *
 * Regenerate with:
 *   7z a -t7z -m0=LZMA2 -ms=on  solid.7z    001.txt 002.txt ComicInfo.xml
 *   7z a -t7z -m0=LZMA2 -ms=off nonsolid.7z 001.txt 002.txt ComicInfo.xml
 *   7z a -t7z -mhe=off -pSECRET enc.7z      001.txt
 */
#include "fixtures_7z.inc"

static bool name_is(u8str_t s, const char *lit) {
    size_t n = strlen(lit);
    return s.len == n && memcmp(s.ptr, lit, n) == 0;
}

static bool body_is(u8str_t s, const char *lit) {
    size_t n = strlen(lit);
    return s.len == n && memcmp(s.ptr, lit, n) == 0;
}

static size_t index_of(const rubraview_sz_archive_t *a, const char *lit) {
    for (size_t i = 0; i < a->entry_count; ++i) {
        if (name_is(a->entries[i].name, lit)) return i;
    }
    return (size_t)-1;
}

int main(void) {
    printf("[test_sevenzip] Starting CB7 archive unit tests...\n");

    size_t mem_size = 1024 * 1024;
    void *raw = malloc(mem_size);
    assert(raw != NULL);
    proven_arena_t arena = proven_arena_create((proven_mem_mut_t){ .ptr = raw, .size = mem_size });

    /* Test 1: a solid archive indexes, and directories are not listed. */
    {
        rubraview_sz_result_t r = rubraview_sz_open(&arena, SZ_SOLID, sizeof(SZ_SOLID), 64u * 1024u * 1024u);
        assert(r.err == RUBRAVIEW_SZ_OK);
        assert(r.value.entry_count == 3);
        assert(index_of(&r.value, "001.txt") != (size_t)-1);
        assert(index_of(&r.value, "002.txt") != (size_t)-1);
        assert(index_of(&r.value, "ComicInfo.xml") != (size_t)-1);
        rubraview_sz_close(&r.value);
        assert(r.value.state == NULL);
    }
    printf("  [PASS] A solid 7z indexes its files by name\n");

    /* Test 2: every file in a solid block reads back its own bytes —
       the block is decoded once and the second read comes out of it. */
    {
        rubraview_sz_result_t r = rubraview_sz_open(&arena, SZ_SOLID, sizeof(SZ_SOLID), 64u * 1024u * 1024u);
        assert(r.err == RUBRAVIEW_SZ_OK);

        size_t one = index_of(&r.value, "001.txt");
        size_t two = index_of(&r.value, "002.txt");
        rubraview_sz_data_result_t a = rubraview_sz_read_entry(&arena, &r.value, one, 1u << 20);
        rubraview_sz_data_result_t b = rubraview_sz_read_entry(&arena, &r.value, two, 1u << 20);
        assert(a.err == RUBRAVIEW_SZ_OK && body_is(a.data, "PAGE-ONE-PAGE-ONE-PAGE-ONE-PAGE-ONE"));
        assert(b.err == RUBRAVIEW_SZ_OK && body_is(b.data, "PAGE-TWO-PAGE-TWO-PAGE-TWO-PAGE-TWO"));

        /* Reading the first one again still gives the same bytes: the
           cached block must not have been consumed by the second read. */
        rubraview_sz_data_result_t again = rubraview_sz_read_entry(&arena, &r.value, one, 1u << 20);
        assert(again.err == RUBRAVIEW_SZ_OK && body_is(again.data, "PAGE-ONE-PAGE-ONE-PAGE-ONE-PAGE-ONE"));

        rubraview_sz_close(&r.value);
    }
    printf("  [PASS] Files inside one solid block read back independently\n");

    /* Test 3: a non-solid archive reads the same way — the caller must
       not be able to tell which one it has. */
    {
        rubraview_sz_result_t r = rubraview_sz_open(&arena, SZ_NONSOLID, sizeof(SZ_NONSOLID), 64u * 1024u * 1024u);
        assert(r.err == RUBRAVIEW_SZ_OK);
        size_t two = index_of(&r.value, "002.txt");
        rubraview_sz_data_result_t b = rubraview_sz_read_entry(&arena, &r.value, two, 1u << 20);
        assert(b.err == RUBRAVIEW_SZ_OK && body_is(b.data, "PAGE-TWO-PAGE-TWO-PAGE-TWO-PAGE-TWO"));
        rubraview_sz_close(&r.value);
    }
    printf("  [PASS] A non-solid 7z reads through the same path\n");

    /* Test 4: §10.2 — the guard is on the block, not the file. A cap
       below the block size refuses before anything is allocated, even
       though each individual file is tiny. */
    {
        rubraview_sz_result_t r = rubraview_sz_open(&arena, SZ_SOLID, sizeof(SZ_SOLID), 8);
        assert(r.err == RUBRAVIEW_SZ_OK);
        rubraview_sz_data_result_t d = rubraview_sz_read_entry(&arena, &r.value, 0, 1u << 20);
        assert(d.err == RUBRAVIEW_SZ_ERR_TOO_LARGE);
        rubraview_sz_close(&r.value);
    }
    printf("  [PASS] A solid block larger than the cap is refused before allocation\n");

    /* Test 5: the per-entry cap still applies on top of the block cap. */
    {
        rubraview_sz_result_t r = rubraview_sz_open(&arena, SZ_SOLID, sizeof(SZ_SOLID), 64u * 1024u * 1024u);
        assert(r.err == RUBRAVIEW_SZ_OK);
        rubraview_sz_data_result_t d = rubraview_sz_read_entry(&arena, &r.value, 0, 4);
        assert(d.err == RUBRAVIEW_SZ_ERR_TOO_LARGE);
        rubraview_sz_close(&r.value);
    }
    printf("  [PASS] An entry larger than the per-entry cap is refused\n");

    /* Test 6: an encrypted archive is refused, not decoded and not
       crashed on. Its header is readable, so it indexes; the content
       coder is what has no implementation here. */
    {
        rubraview_sz_result_t r = rubraview_sz_open(&arena, SZ_ENCRYPTED, sizeof(SZ_ENCRYPTED), 64u * 1024u * 1024u);
        if (r.err == RUBRAVIEW_SZ_OK) {
            rubraview_sz_data_result_t d = rubraview_sz_read_entry(&arena, &r.value, 0, 1u << 20);
            assert(d.err == RUBRAVIEW_SZ_ERR_UNSUPPORTED_CODER);
            rubraview_sz_close(&r.value);
        } else {
            assert(r.err == RUBRAVIEW_SZ_ERR_UNSUPPORTED_CODER || r.err == RUBRAVIEW_SZ_ERR_CORRUPT);
        }
    }
    printf("  [PASS] An encrypted 7z is refused rather than decoded\n");

    /* Test 7: anything that is not a 7z is rejected on its signature. */
    {
        const uint8_t not_7z[64] = { 'P', 'K', 3, 4 };
        rubraview_sz_result_t r = rubraview_sz_open(&arena, not_7z, sizeof(not_7z), 1u << 20);
        assert(r.err == RUBRAVIEW_SZ_ERR_NOT_A_7Z);
        rubraview_sz_close(&r.value); /* must be safe after a failed open */
    }
    printf("  [PASS] A non-7z buffer is rejected and closing it is safe\n");

    /* Test 8: a truncated archive fails to open rather than reading off
       the end of the buffer — ASan is the real assertion here. */
    {
        rubraview_sz_result_t r = rubraview_sz_open(&arena, SZ_SOLID, sizeof(SZ_SOLID) / 2, 1u << 20);
        assert(r.err != RUBRAVIEW_SZ_OK);
        rubraview_sz_close(&r.value);
    }
    printf("  [PASS] A truncated 7z fails to open without reading past the buffer\n");

    /* Test 9: a corrupted compressed stream is caught by the CRC the
       format carries, not passed on as pages. */
    {
        uint8_t *damaged = (uint8_t*)malloc(sizeof(SZ_SOLID));
        assert(damaged != NULL);
        memcpy(damaged, SZ_SOLID, sizeof(SZ_SOLID));
        damaged[40] ^= 0xFFu;  /* inside the packed stream, past the 32-byte header */

        rubraview_sz_result_t r = rubraview_sz_open(&arena, damaged, sizeof(SZ_SOLID), 64u * 1024u * 1024u);
        if (r.err == RUBRAVIEW_SZ_OK) {
            rubraview_sz_data_result_t d = rubraview_sz_read_entry(&arena, &r.value, 0, 1u << 20);
            assert(d.err != RUBRAVIEW_SZ_OK);
            rubraview_sz_close(&r.value);
        }
        free(damaged);
    }
    printf("  [PASS] A damaged packed stream fails instead of yielding wrong bytes\n");

    /* Test 10: an out-of-range index is refused. */
    {
        rubraview_sz_result_t r = rubraview_sz_open(&arena, SZ_SOLID, sizeof(SZ_SOLID), 1u << 20);
        assert(r.err == RUBRAVIEW_SZ_OK);
        rubraview_sz_data_result_t d = rubraview_sz_read_entry(&arena, &r.value, 99, 1u << 20);
        assert(d.err == RUBRAVIEW_SZ_ERR_BAD_INDEX);
        rubraview_sz_close(&r.value);
    }
    printf("  [PASS] An out-of-range entry index is refused\n");

    free(raw);
    printf("[test_sevenzip] All tests passed successfully!\n");
    return 0;
}
