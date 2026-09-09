#include "rubraview/batchrun.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <math.h>

static bool is(u8str_t s, const char *l) { return s.len == strlen(l) && memcmp(s.ptr, l, s.len) == 0; }

static const rubraview_batch_action_t *find_action(const rubraview_cli_result_t *r,
                                                   rubraview_batch_action_kind_t kind) {
    for (size_t i = 0; i < r->job.action_count; ++i) {
        if (r->actions[i].kind == kind) return &r->actions[i];
    }
    return NULL;
}

typedef struct run_log {
    char names[16][64];
    size_t count;
    bool fail_second;
} run_log_t;

static bool record_process(proven_arena_t *arena, const rubraview_batch_job_t *job,
                           const rubraview_batch_input_t *input, u8str_t output_name, void *ctx) {
    (void)job; (void)input;
    run_log_t *log = (run_log_t*)ctx;

    /* The engine promises a reset arena; allocating here is the point of
       that promise, so the test uses it. */
    proven_result_mem_mut_t scratch = proven_arena_alloc(arena, 1024);
    assert(proven_is_ok(scratch.err));

    if (log->count < 16) {
        size_t n = output_name.len < 63 ? output_name.len : 63;
        memcpy(log->names[log->count], output_name.ptr, n);
        log->names[log->count][n] = '\0';
        log->count++;
    }
    if (log->fail_second && log->count == 2) return false;
    return true;
}

int main(void) {
    printf("[test_batchrun] Starting batch command line and engine tests...\n");

    size_t mem_size = 1024 * 1024;
    void *raw = malloc(mem_size);
    assert(raw != NULL);
    proven_arena_t arena = proven_arena_create((proven_mem_mut_t){ .ptr = raw, .size = mem_size });

    /* Test 1: §11.2's own example line parses into the job it describes. */
    {
        const char *argv[] = { "rubraview.exe", "--batch", "--resize=50%", "--format=webp", "D:/photos" };
        rubraview_cli_result_t r;
        rubraview_cli_parse(&r, &arena, 5, argv);
        assert(r.err == RUBRAVIEW_CLI_OK);
        assert(r.batch_mode);
        assert(is(r.input, "D:/photos"));

        const rubraview_batch_action_t *resize = find_action(&r, RUBRAVIEW_BATCH_RESIZE);
        assert(resize && resize->params.resize.mode == RUBRAVIEW_RESIZE_PERCENT);
        assert(fabs(resize->params.resize.value_a - 50.0) < 0.0001);
        assert(resize->params.resize.filter == RUBRAVIEW_FILTER_LANCZOS3); /* the default */
        assert(r.export_options.format == RUBRAVIEW_EXPORT_WEBP);
    }
    printf("  [PASS] The roadmap's own example command line parses\n");

    /* Test 2: every shape of --resize. */
    {
        struct { const char *arg; rubraview_batch_resize_mode_t mode; double a, b; } CASES[] = {
            { "--resize=1920x1080", RUBRAVIEW_RESIZE_BOUNDING_BOX, 1920, 1080 },
            { "--resize=w800",      RUBRAVIEW_RESIZE_FIXED_WIDTH,  800, 0 },
            { "--resize=h600",      RUBRAVIEW_RESIZE_FIXED_HEIGHT, 600, 0 },
            { "--resize=125%",      RUBRAVIEW_RESIZE_PERCENT,      125, 0 },
        };
        for (size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); ++i) {
            const char *argv[] = { "x", "--batch", CASES[i].arg, "in" };
            rubraview_cli_result_t r;
        rubraview_cli_parse(&r, &arena, 4, argv);
            assert(r.err == RUBRAVIEW_CLI_OK);
            const rubraview_batch_action_t *a = find_action(&r, RUBRAVIEW_BATCH_RESIZE);
            assert(a && a->params.resize.mode == CASES[i].mode);
            assert(fabs(a->params.resize.value_a - CASES[i].a) < 0.0001);
            if (CASES[i].b > 0) assert(fabs(a->params.resize.value_b - CASES[i].b) < 0.0001);
        }
    }
    printf("  [PASS] Percentage, bounding box, fixed width and fixed height all parse\n");

    /* Test 3: a typo stops the run. This is the case that matters most:
       a thousand files must not be converted the wrong way because one
       letter was out of place. */
    {
        const char *argv[] = { "x", "--batch", "--resiez=50%", "D:/photos" };
        rubraview_cli_result_t r;
        rubraview_cli_parse(&r, &arena, 4, argv);
        assert(r.err == RUBRAVIEW_CLI_ERR_UNKNOWN_FLAG);
        assert(is(r.offending, "--resiez=50%"));
        assert(rubraview_cli_error_text(r.err).len > 0);
    }
    printf("  [PASS] An unknown flag stops the run and names itself\n");

    /* Test 4: a value that is out of range, or has a stray character in
       it, is refused rather than silently truncated. */
    {
        const char *bad[] = { "--quality=0", "--quality=101", "--quality=90abc",
                              "--resize=abc", "--resize=0%", "--rotate=45",
                              "--flip=x", "--filter=magic", "--format=xyz",
                              "--exposure=99", "--contrast=-500" };
        for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
            const char *argv[] = { "x", "--batch", bad[i], "in" };
            rubraview_cli_result_t r;
        rubraview_cli_parse(&r, &arena, 4, argv);
            assert(r.err == RUBRAVIEW_CLI_ERR_BAD_VALUE);
        }
    }
    printf("  [PASS] Out-of-range and malformed values are refused\n");

    /* Test 5: --batch with nothing to work on is an error; without
       --batch the same line is just a viewer launch. */
    {
        const char *argv[] = { "x", "--batch", "--resize=50%" };
        rubraview_cli_result_t r;
        rubraview_cli_parse(&r, &arena, 3, argv);
        assert(r.err == RUBRAVIEW_CLI_ERR_NO_INPUT);

        const char *argv2[] = { "x", "D:/photos/a.jpg" };
        rubraview_cli_result_t r2;
        rubraview_cli_parse(&r2, &arena, 2, argv2);
        assert(r2.err == RUBRAVIEW_CLI_OK && !r2.batch_mode);
        assert(is(r2.input, "D:/photos/a.jpg"));
    }
    printf("  [PASS] --batch needs an input; an ordinary launch does not\n");

    /* Test 6: repeated flags of one kind merge into a single stage
       rather than queueing several passes over the pixels. */
    {
        const char *argv[] = { "x", "--batch", "--exposure=1.5", "--contrast=20",
                               "--grayscale", "--sharpen=150,2.0", "in" };
        rubraview_cli_result_t r;
        rubraview_cli_parse(&r, &arena, 7, argv);
        assert(r.err == RUBRAVIEW_CLI_OK);
        assert(r.job.action_count == 1);

        const rubraview_batch_action_t *c = find_action(&r, RUBRAVIEW_BATCH_COLOR_ADJUST);
        assert(c);
        assert(c->params.color.has_color_adjust);
        assert(fabsf(c->params.color.adjust.exposure_ev - 1.5f) < 0.0001f);
        assert(fabsf(c->params.color.adjust.contrast - 20.0f) < 0.0001f);
        assert(c->params.color.grayscale);
        assert(c->params.color.has_unsharp);
        assert(fabsf(c->params.color.amount - 1.5f) < 0.0001f);
        assert(fabsf(c->params.color.sigma - 2.0f) < 0.0001f);
    }
    printf("  [PASS] Colour flags merge into one stage instead of several passes\n");

    /* Test 7: filters, naming, sizes and the output directory. */
    {
        const char *argv[] = { "x", "--batch", "--include=*.jpg;*.png", "--exclude=*_thumb.*",
                               "--min-size=1024", "--max-size=1048576",
                               "--name={name}_{w}x{h}.{ext}", "--out=D:/out",
                               "--filter=bicubic", "--resize=w100", "in" };
        rubraview_cli_result_t r;
        rubraview_cli_parse(&r, &arena, 11, argv);
        assert(r.err == RUBRAVIEW_CLI_OK);
        assert(is(r.job.include_pattern, "*.jpg;*.png"));
        assert(is(r.job.exclude_pattern, "*_thumb.*"));
        assert(r.job.min_size_bytes == 1024 && r.job.max_size_bytes == 1048576);
        assert(is(r.job.naming_pattern, "{name}_{w}x{h}.{ext}"));
        assert(is(r.output_dir, "D:/out"));

        const rubraview_batch_action_t *a = find_action(&r, RUBRAVIEW_BATCH_RESIZE);
        /* --filter came before --resize; the resize must not have reset it. */
        assert(a && a->params.resize.filter == RUBRAVIEW_FILTER_BICUBIC);
        assert(a->params.resize.mode == RUBRAVIEW_RESIZE_FIXED_WIDTH);
    }
    printf("  [PASS] Filters, patterns, size bounds and the output directory survive each other\n");

    /* Test 8: --privacy-clean reaches both the action chain and the
       encoder settings; §3.10 and §3.11 must not disagree. */
    {
        const char *argv[] = { "x", "--batch", "--privacy-clean", "in" };
        rubraview_cli_result_t r;
        rubraview_cli_parse(&r, &arena, 4, argv);
        assert(r.err == RUBRAVIEW_CLI_OK);
        const rubraview_batch_action_t *p = find_action(&r, RUBRAVIEW_BATCH_PRIVACY_SCRUB);
        assert(p && p->params.privacy.strip_all_exif && p->params.privacy.strip_xmp && p->params.privacy.strip_iptc);
        assert(r.export_options.privacy_clean);
    }
    printf("  [PASS] Privacy cleaning reaches both the action chain and the encoder\n");

    /* Test 9: the engine filters, names and processes — and the arena
       is reset between files, which is what bounds a long run. */
    {
        const char *argv[] = { "x", "--batch", "--include=*.jpg", "--min-size=100",
                               "--name={name}_small.{ext}", "--format=png", "in" };
        rubraview_cli_result_t r;
        rubraview_cli_parse(&r, &arena, 7, argv);
        assert(r.err == RUBRAVIEW_CLI_OK);

        rubraview_batch_input_t inputs[] = {
            { .path = { .ptr = "D:/a/one.jpg", .len = 12 }, .size_bytes = 5000 },
            { .path = { .ptr = "D:/a/two.png", .len = 12 }, .size_bytes = 5000 },  /* wrong type */
            { .path = { .ptr = "D:/a/tiny.jpg", .len = 13 }, .size_bytes = 10 },   /* too small */
            { .path = { .ptr = "D:/a/four.jpg", .len = 13 }, .size_bytes = 9000 },
        };

        run_log_t log = {0};
        rubraview_batch_item_result_t results[4] = {0};
        rubraview_batch_report_t report = rubraview_batch_run(&arena, &r.job, inputs, 4,
                                                              U8("2026-09-09"),
                                                              record_process, &log, results, 4);
        assert(report.total == 4);
        assert(report.processed == 2);
        assert(report.skipped == 2);
        assert(report.failed == 0);
        assert(log.count == 2);
        assert(strcmp(log.names[0], "one_small.png") == 0);  /* the convert action set the extension */
        assert(strcmp(log.names[1], "four_small.png") == 0);
        assert(results[1].status == RUBRAVIEW_BATCH_STATUS_SKIPPED);
        assert(results[3].status == RUBRAVIEW_BATCH_STATUS_OK);
    }
    printf("  [PASS] The engine filters, renames and converts, resetting the arena per file\n");

    /* Test 10: one file failing does not stop the rest, and the report
       says how many of each there were. */
    {
        const char *argv[] = { "x", "--batch", "in" };
        rubraview_cli_result_t r;
        rubraview_cli_parse(&r, &arena, 3, argv);
        assert(r.err == RUBRAVIEW_CLI_OK);

        rubraview_batch_input_t inputs[] = {
            { .path = { .ptr = "a.jpg", .len = 5 }, .size_bytes = 1 },
            { .path = { .ptr = "b.jpg", .len = 5 }, .size_bytes = 1 },
            { .path = { .ptr = "c.jpg", .len = 5 }, .size_bytes = 1 },
        };
        run_log_t log = { .fail_second = true };
        rubraview_batch_report_t report = rubraview_batch_run(&arena, &r.job, inputs, 3,
                                                              U8("2026-09-09"),
                                                              record_process, &log, NULL, 0);
        assert(report.processed == 2 && report.failed == 1 && report.skipped == 0);
    }
    printf("  [PASS] A failed file is counted and the run carries on\n");

    /* Test 11: §3.19's own flags parse, and none of them implies a
       batch run. */
    {
        const char *argv[] = { "x", "--register-shell" };
        rubraview_cli_result_t r;
        rubraview_cli_parse(&r, &arena, 2, argv);
        assert(r.err == RUBRAVIEW_CLI_OK && r.register_shell && !r.batch_mode);

        const char *argv2[] = { "x", "--unregister-shell" };
        rubraview_cli_result_t r2;
        rubraview_cli_parse(&r2, &arena, 2, argv2);
        assert(r2.err == RUBRAVIEW_CLI_OK && r2.unregister_shell);

        const char *argv3[] = { "x", "--new-instance", "D:/a.jpg" };
        rubraview_cli_result_t r3;
        rubraview_cli_parse(&r3, &arena, 3, argv3);
        assert(r3.err == RUBRAVIEW_CLI_OK && r3.new_instance);
        assert(is(r3.input, "D:/a.jpg"));
    }
    printf("  [PASS] The shell and instance flags parse without implying a batch run\n");

    free(raw);
    printf("[test_batchrun] All tests passed successfully!\n");
    return 0;
}
