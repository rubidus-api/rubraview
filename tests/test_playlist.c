#include "rubraview/playlist.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <math.h>

static u8str_t lit(const char *s) {
    return (u8str_t){ .ptr = s, .len = strlen(s) };
}

static bool str_eq(u8str_t s, const char *l) {
    size_t n = strlen(l);
    return s.len == n && (n == 0 || memcmp(s.ptr, l, n) == 0);
}

static bool approx(double a, double b) { return fabs(a - b) < 1e-6; }

int main(void) {
    printf("[test_playlist] Starting playlist parser/serializer unit tests...\n");

    size_t mem_size = 64 * 1024;
    void *raw_mem = malloc(mem_size);
    assert(raw_mem != NULL);
    proven_arena_t arena = proven_arena_create((proven_mem_mut_t){ .ptr = raw_mem, .size = mem_size });

    /* Test 1: .rvlist — one path per line, comments and blank lines skipped. */
    {
        const char *text =
            "; My Favorites\n"
            "Photos/2026/sunset.jpg\n"
            "\n"
            "Comics/Berserk_Vol01.cbz\n"
            "; a trailing comment\n"
            "Video/clip.mp4\n";

        rubraview_playlist_t pl = rubraview_playlist_parse_rvlist(&arena, lit(text));
        assert(pl.count == 3);
        assert(str_eq(pl.entries[0].path, "Photos/2026/sunset.jpg"));
        assert(str_eq(pl.entries[1].path, "Comics/Berserk_Vol01.cbz"));
        assert(str_eq(pl.entries[2].path, "Video/clip.mp4"));
        assert(pl.entries[0].duration_seconds < 0); /* .rvlist has no duration concept */
    }
    printf("  [PASS] .rvlist parses paths, skipping comments and blank lines\n");

    /* Test 2: .rvlist serialize -> re-parse round-trips the path list. */
    {
        rubraview_playlist_entry_t entries[2] = {
            { .path = lit("a/b.jpg"), .duration_seconds = -1, .title = { .ptr = "", .len = 0 } },
            { .path = lit("c/d.png"), .duration_seconds = -1, .title = { .ptr = "", .len = 0 } },
        };
        rubraview_playlist_t pl = { .entries = entries, .count = 2 };

        u8str_t serialized = rubraview_playlist_serialize_rvlist(&arena, &pl);
        assert(serialized.ptr[serialized.len] == '\0');

        rubraview_playlist_t reparsed = rubraview_playlist_parse_rvlist(&arena, serialized);
        assert(reparsed.count == 2);
        assert(str_eq(reparsed.entries[0].path, "a/b.jpg"));
        assert(str_eq(reparsed.entries[1].path, "c/d.png"));
    }
    printf("  [PASS] .rvlist serialize -> re-parse round-trip\n");

    /* Test 3: .m3u8 — EXTM3U header, EXTINF-prefixed entries, a bare path
       with no EXTINF, and an unrelated tag are all handled correctly. */
    {
        const char *text =
            "#EXTM3U\n"
            "#EXTINF:213,Artist Name - Song Title\n"
            "Music/song.flac\n"
            "#EXTGRP:Favorites\n"
            "#EXTINF:-1,Live Stream\n"
            "http://example.internal/stream.m3u8\n"
            "Video/no_extinf_clip.mp4\n";

        rubraview_playlist_t pl = rubraview_playlist_parse_m3u8(&arena, lit(text));
        assert(pl.count == 3);

        assert(str_eq(pl.entries[0].path, "Music/song.flac"));
        assert(approx(pl.entries[0].duration_seconds, 213.0));
        assert(str_eq(pl.entries[0].title, "Artist Name - Song Title"));

        assert(str_eq(pl.entries[1].path, "http://example.internal/stream.m3u8"));
        assert(approx(pl.entries[1].duration_seconds, -1.0));
        assert(str_eq(pl.entries[1].title, "Live Stream"));

        assert(str_eq(pl.entries[2].path, "Video/no_extinf_clip.mp4"));
        assert(pl.entries[2].duration_seconds < 0); /* no preceding EXTINF */
        assert(pl.entries[2].title.len == 0);
    }
    printf("  [PASS] .m3u8 parses EXTINF duration/title, bare paths, and ignores other tags\n");

    /* Test 4: .m3u8 serialize -> re-parse round-trips duration and title. */
    {
        rubraview_playlist_entry_t entries[2] = {
            { .path = lit("Music/track1.mp3"), .duration_seconds = 180.0, .title = lit("Track One") },
            { .path = lit("Images/photo.jpg"), .duration_seconds = -1.0, .title = { .ptr = "", .len = 0 } },
        };
        rubraview_playlist_t pl = { .entries = entries, .count = 2 };

        u8str_t serialized = rubraview_playlist_serialize_m3u8(&arena, &pl);
        assert(serialized.len >= 8 && memcmp(serialized.ptr, "#EXTM3U\n", 8) == 0);

        rubraview_playlist_t reparsed = rubraview_playlist_parse_m3u8(&arena, serialized);
        assert(reparsed.count == 2);
        assert(str_eq(reparsed.entries[0].path, "Music/track1.mp3"));
        assert(approx(reparsed.entries[0].duration_seconds, 180.0));
        assert(str_eq(reparsed.entries[0].title, "Track One"));
        assert(str_eq(reparsed.entries[1].path, "Images/photo.jpg"));
    }
    printf("  [PASS] .m3u8 serialize -> re-parse round-trip preserves duration and title\n");

    /* Test 5: Mixed media types (image, comic archive, video, audio) all
       coexist in one playlist without special handling by this module. */
    {
        const char *text =
            "cover.jpg\n"
            "chapter01.cbz\n"
            "opening.mp4\n"
            "theme.flac\n"
            "animated.webp\n";
        rubraview_playlist_t pl = rubraview_playlist_parse_rvlist(&arena, lit(text));
        assert(pl.count == 5);
    }
    printf("  [PASS] Mixed still/comic/video/audio paths coexist in one playlist\n");

    /* Test 6: Empty input produces zero entries, no crash. */
    {
        rubraview_playlist_t rv = rubraview_playlist_parse_rvlist(&arena, (u8str_t){ .ptr = "", .len = 0 });
        assert(rv.count == 0);
        rubraview_playlist_t m3u = rubraview_playlist_parse_m3u8(&arena, (u8str_t){ .ptr = "", .len = 0 });
        assert(m3u.count == 0);
    }
    printf("  [PASS] Empty input produces zero entries, no crash\n");

    free(raw_mem);
    printf("[test_playlist] All tests passed successfully!\n");
    return 0;
}
