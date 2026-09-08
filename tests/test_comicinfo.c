#include "rubraview/comicinfo.h"
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

int main(void) {
    printf("[test_comicinfo] Starting ComicInfo.xml minimal reader unit tests...\n");

    size_t mem_size = 64 * 1024;
    void *raw_mem = malloc(mem_size);
    assert(raw_mem != NULL);
    proven_arena_t arena = proven_arena_create((proven_mem_mut_t){ .ptr = raw_mem, .size = mem_size });

    /* Test 1: A full, realistic ComicInfo.xml with Manga RTL and three
       Page elements including a FrontCover. */
    {
        const char *xml =
            "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
            "<ComicInfo xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">\n"
            "  <Title>Berserk</Title>\n"
            "  <Series>Berserk</Series>\n"
            "  <Volume>1</Volume>\n"
            "  <Writer>Kentaro Miura</Writer>\n"
            "  <Manga>YesAndRightToLeft</Manga>\n"
            "  <Pages>\n"
            "    <Page Image=\"0\" Type=\"FrontCover\" />\n"
            "    <Page Image=\"1\" />\n"
            "    <Page Image=\"2\" Type=\"Story\" />\n"
            "  </Pages>\n"
            "</ComicInfo>\n";

        rubraview_comicinfo_t info = rubraview_comicinfo_parse(&arena, lit(xml));
        assert(str_eq(info.title, "Berserk"));
        assert(str_eq(info.series, "Berserk"));
        assert(info.volume == 1);
        assert(info.manga == RUBRAVIEW_MANGA_YES_RTL);
        assert(info.page_count == 3);
        assert(info.pages[0].image_index == 0);
        assert(str_eq(info.pages[0].type, "FrontCover"));
        assert(info.pages[1].image_index == 1);
        assert(info.pages[1].type.len == 0); /* no Type attribute */
        assert(info.pages[2].image_index == 2);
        assert(str_eq(info.pages[2].type, "Story"));
    }
    printf("  [PASS] Full document: Title/Series/Volume/Manga RTL, Page attributes\n");

    /* Test 2: Manga "Yes" (without RTL) and "No" both classify distinctly
       from "YesAndRightToLeft", per §3.8.5 point 2. */
    {
        rubraview_comicinfo_t yes = rubraview_comicinfo_parse(&arena, lit("<ComicInfo><Manga>Yes</Manga></ComicInfo>"));
        assert(yes.manga == RUBRAVIEW_MANGA_YES);

        rubraview_comicinfo_t no = rubraview_comicinfo_parse(&arena, lit("<ComicInfo><Manga>No</Manga></ComicInfo>"));
        assert(no.manga == RUBRAVIEW_MANGA_NO);
    }
    printf("  [PASS] Manga \"Yes\" and \"No\" classify distinctly from RTL\n");

    /* Test 3: A missing <Manga> tag defaults to MANGA_NO (Western LTR). */
    {
        rubraview_comicinfo_t info = rubraview_comicinfo_parse(&arena, lit("<ComicInfo><Title>NoMangaTag</Title></ComicInfo>"));
        assert(info.manga == RUBRAVIEW_MANGA_NO);
        assert(str_eq(info.title, "NoMangaTag"));
    }
    printf("  [PASS] Missing <Manga> tag defaults to Western LTR\n");

    /* Test 4: No <Pages> block at all -> zero pages, no crash. */
    {
        rubraview_comicinfo_t info = rubraview_comicinfo_parse(&arena, lit("<ComicInfo><Title>Solo</Title></ComicInfo>"));
        assert(info.page_count == 0);
    }
    printf("  [PASS] Document with no Pages block yields zero pages\n");

    /* Test 5: A non-numeric Volume falls back to -1 (absent). */
    {
        rubraview_comicinfo_t info = rubraview_comicinfo_parse(&arena, lit("<ComicInfo><Volume>N/A</Volume></ComicInfo>"));
        assert(info.volume == -1);
    }
    printf("  [PASS] Non-numeric Volume falls back to -1\n");

    /* Test 6: Empty and NULL input handled without crashing, all defaults. */
    {
        rubraview_comicinfo_t empty = rubraview_comicinfo_parse(&arena, (u8str_t){ .ptr = "", .len = 0 });
        assert(empty.manga == RUBRAVIEW_MANGA_NO);
        assert(empty.volume == -1);
        assert(empty.page_count == 0);
    }
    printf("  [PASS] Empty input handled without crashing, all fields default\n");

    /* Test 7: Many Page elements exercise the growable page buffer past
       its initial capacity. */
    {
        char big_xml[4096];
        size_t pos = (size_t)snprintf(big_xml, sizeof(big_xml), "<ComicInfo><Pages>");
        for (int i = 0; i < 20; ++i) {
            pos += (size_t)snprintf(big_xml + pos, sizeof(big_xml) - pos, "<Page Image=\"%d\" />", i);
        }
        pos += (size_t)snprintf(big_xml + pos, sizeof(big_xml) - pos, "</Pages></ComicInfo>");

        rubraview_comicinfo_t info = rubraview_comicinfo_parse(&arena, (u8str_t){ .ptr = big_xml, .len = pos });
        assert(info.page_count == 20);
        for (int i = 0; i < 20; ++i) {
            assert(info.pages[i].image_index == i);
        }
    }
    printf("  [PASS] Page buffer grows correctly past initial capacity\n");

    /* Test 8: §3.8.5 point 2 — a manga archive switches the reader to
       Book mode, right to left, with no key press. */
    {
        rubraview_comicinfo_t info = rubraview_comicinfo_parse(&arena,
            lit("<ComicInfo><Manga>YesAndRightToLeft</Manga></ComicInfo>"));

        rubraview_layout_opts_t opts = rubraview_layout_opts_default(RUBRAVIEW_PAGE_LAYOUT_SINGLE, RUBRAVIEW_READING_LTR);
        rubraview_comicinfo_apply(&info, &opts, NULL, 0);
        assert(opts.mode == RUBRAVIEW_PAGE_LAYOUT_BOOK);
        assert(opts.direction == RUBRAVIEW_READING_RTL);
    }
    printf("  [PASS] Manga RTL metadata switches the layout to Book mode, right to left\n");

    /* Test 9: metadata that says nothing changes nothing. */
    {
        rubraview_comicinfo_t plain = rubraview_comicinfo_parse(&arena, lit("<ComicInfo><Title>x</Title></ComicInfo>"));
        rubraview_layout_opts_t opts = rubraview_layout_opts_default(RUBRAVIEW_PAGE_LAYOUT_DUAL, RUBRAVIEW_READING_LTR);
        rubraview_comicinfo_apply(&plain, &opts, NULL, 0);
        assert(opts.mode == RUBRAVIEW_PAGE_LAYOUT_DUAL);
        assert(opts.direction == RUBRAVIEW_READING_LTR);
    }
    printf("  [PASS] Metadata with no <Manga> tag leaves the layout untouched\n");

    /* Test 10: §3.8.5 point 3 — pages tagged as covers are marked to
       stand alone, whatever their number, and other pages are not. */
    {
        rubraview_comicinfo_t info = rubraview_comicinfo_parse(&arena, lit(
            "<ComicInfo><Pages>"
            "<Page Image=\"0\" Type=\"FrontCover\" />"
            "<Page Image=\"1\" Type=\"Story\" />"
            "<Page Image=\"5\" Type=\"InnerCover\" />"
            "</Pages></ComicInfo>"));

        rubraview_page_info_t pages[8] = {0};
        for (int i = 0; i < 8; ++i) { pages[i].width = 800; pages[i].height = 1200; }

        rubraview_comicinfo_apply(&info, NULL, pages, 8);
        assert(pages[0].force_standalone);
        assert(!pages[1].force_standalone);
        assert(pages[5].force_standalone);
        assert(!pages[7].force_standalone);
    }
    printf("  [PASS] FrontCover and InnerCover pages are marked to stand alone\n");

    /* Test 11: a Page index outside the archive is ignored rather than
       writing past the array. */
    {
        rubraview_comicinfo_t info = rubraview_comicinfo_parse(&arena, lit(
            "<ComicInfo><Pages><Page Image=\"99\" Type=\"FrontCover\" /></Pages></ComicInfo>"));
        rubraview_page_info_t pages[2] = {0};
        rubraview_comicinfo_apply(&info, NULL, pages, 2);
        assert(!pages[0].force_standalone && !pages[1].force_standalone);
    }
    printf("  [PASS] An out-of-range Page index is ignored, not written past the array\n");

    free(raw_mem);
    printf("[test_comicinfo] All tests passed successfully!\n");
    return 0;
}
