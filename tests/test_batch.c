#include "rubraview/batch.h"
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
    printf("[test_batch] Starting batch job model unit tests...\n");

    size_t mem_size = 64 * 1024;
    void *raw_mem = malloc(mem_size);
    assert(raw_mem != NULL);
    proven_arena_t arena = proven_arena_create((proven_mem_mut_t){ .ptr = raw_mem, .size = mem_size });

    /* Test 1: Include pattern filters by extension. */
    {
        rubraview_batch_job_t job = { .include_pattern = lit("*.jpg;*.png") };
        assert(rubraview_batch_file_matches(&job, lit("photo.jpg"), 100));
        assert(!rubraview_batch_file_matches(&job, lit("video.mp4"), 100));
    }
    printf("  [PASS] Include pattern filters by extension\n");

    /* Test 2: Exclude pattern overrides a matching include. */
    {
        rubraview_batch_job_t job = { .include_pattern = lit("*.jpg"), .exclude_pattern = lit("*_thumb.jpg") };
        assert(rubraview_batch_file_matches(&job, lit("page.jpg"), 100));
        assert(!rubraview_batch_file_matches(&job, lit("page_thumb.jpg"), 100));
    }
    printf("  [PASS] Exclude pattern overrides a matching include\n");

    /* Test 3: Min/max file size bounds. */
    {
        rubraview_batch_job_t job = { .min_size_bytes = 1000, .max_size_bytes = 5000 };
        assert(!rubraview_batch_file_matches(&job, lit("tiny.jpg"), 500));
        assert(rubraview_batch_file_matches(&job, lit("mid.jpg"), 2000));
        assert(!rubraview_batch_file_matches(&job, lit("huge.jpg"), 9000));
    }
    printf("  [PASS] Min/max file size bounds enforced\n");

    /* Test 4: An unconfigured job (no filters) matches everything. */
    {
        rubraview_batch_job_t job = {0};
        assert(rubraview_batch_file_matches(&job, lit("anything.xyz"), 123456));
    }
    printf("  [PASS] Unconfigured job matches every file\n");

    /* Test 5: Naming pattern substitution — {name}/{ext}. */
    {
        u8str_t result = rubraview_batch_format_name(&arena, lit("{name}_thumb.{ext}"), lit("vacation"), lit(".webp"), 0, 0, lit(""));
        assert(str_eq(result, "vacation_thumb.webp"));
    }
    printf("  [PASS] {name}/{ext} substitution (leading dot stripped from ext)\n");

    /* Test 6: Naming pattern substitution — {date}/{name}/{w}/{h}/{ext}. */
    {
        u8str_t result = rubraview_batch_format_name(&arena, lit("{date}_{name}_{w}x{h}.{ext}"), lit("page01"), lit(".png"), 1920, 1080, lit("2026-09-08"));
        assert(str_eq(result, "2026-09-08_page01_1920x1080.png"));
    }
    printf("  [PASS] Full pattern with date, dimensions, name, and extension\n");

    /* Test 7: An unrecognized token is copied through unchanged. */
    {
        u8str_t result = rubraview_batch_format_name(&arena, lit("{name}_{unknown}.{ext}"), lit("x"), lit(".jpg"), 0, 0, lit(""));
        assert(str_eq(result, "x_{unknown}.jpg"));
    }
    printf("  [PASS] Unrecognized token copied through unchanged\n");

    /* Test 8: An unclosed '{' is copied through unchanged, no crash. */
    {
        u8str_t result = rubraview_batch_format_name(&arena, lit("{name}_broken{"), lit("x"), lit(".jpg"), 0, 0, lit(""));
        assert(str_eq(result, "x_broken{"));
    }
    printf("  [PASS] Unclosed brace copied through unchanged, no crash\n");

    /* Test 9: Result carries the null-terminated allocation invariant. */
    {
        u8str_t result = rubraview_batch_format_name(&arena, lit("{name}.{ext}"), lit("f"), lit(".png"), 0, 0, lit(""));
        assert(result.ptr[result.len] == '\0');
    }
    printf("  [PASS] Result carries the null-terminated allocation invariant\n");

    /* Test 10: The action chain data model holds every action kind
       (a construction/shape smoke test for the tagged union). */
    {
        rubraview_batch_action_t orient = {
            .kind = RUBRAVIEW_BATCH_ORIENT,
            .params.orient = { .rotate_degrees = 90, .flip_horizontal = true, .flip_vertical = false, .use_exif_auto_orient = false },
        };
        rubraview_batch_action_t resize = {
            .kind = RUBRAVIEW_BATCH_RESIZE,
            .params.resize = { .mode = RUBRAVIEW_RESIZE_BOUNDING_BOX, .value_a = 1920, .value_b = 1080, .filter = RUBRAVIEW_FILTER_LANCZOS3 },
        };
        rubraview_batch_action_t color = {
            .kind = RUBRAVIEW_BATCH_COLOR_ADJUST,
            .params.color = { .has_color_adjust = true, .adjust = { .exposure_ev = 0.5f, .contrast = 10.0f, .saturation = 1.1f, .gamma = 1.0f }, .grayscale = false },
        };
        rubraview_batch_action_t privacy = {
            .kind = RUBRAVIEW_BATCH_PRIVACY_SCRUB,
            .params.privacy = { .strip_all_exif = true, .strip_xmp = true, .strip_iptc = true },
        };
        rubraview_batch_action_t convert = {
            .kind = RUBRAVIEW_BATCH_CONVERT,
            .params.convert = { .target_ext = lit("webp"), .quality = 90 },
        };

        rubraview_batch_action_t actions[5] = { orient, resize, color, privacy, convert };
        rubraview_batch_job_t job = {
            .actions = actions, .action_count = 5,
            .include_pattern = lit("*.jpg;*.png"),
            .naming_pattern = lit("{name}_out.{ext}"),
        };
        assert(job.action_count == 5);
        assert(job.actions[0].kind == RUBRAVIEW_BATCH_ORIENT);
        assert(job.actions[1].params.resize.value_a == 1920);
        assert(job.actions[2].params.color.adjust.contrast == 10.0f);
        assert(job.actions[3].params.privacy.strip_all_exif);
        assert(str_eq(job.actions[4].params.convert.target_ext, "webp"));
    }
    printf("  [PASS] Action chain holds every action kind (orient/resize/color/privacy/convert)\n");

    free(raw_mem);
    printf("[test_batch] All tests passed successfully!\n");
    return 0;
}
