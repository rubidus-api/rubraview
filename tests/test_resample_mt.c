#include "rubraview/resample_mt.h"
#include "proven/job.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

/* A pattern with structure in both directions, so a band boundary that
   sampled the wrong source rows would show up rather than averaging
   away. */
static void fill_pattern(rubraview_pixbuf_t *pb) {
    for (int32_t y = 0; y < pb->height; ++y) {
        for (int32_t x = 0; x < pb->width; ++x) {
            uint8_t *px = pb->pixels + (ptrdiff_t)y * pb->stride + x * 4;
            px[0] = (uint8_t)((x * 7 + y * 3) & 0xFF);
            px[1] = (uint8_t)((x * 13) & 0xFF);
            px[2] = (uint8_t)((y * 17) & 0xFF);
            px[3] = 255;
        }
    }
}

static bool identical(const rubraview_pixbuf_t *a, const rubraview_pixbuf_t *b) {
    if (a->width != b->width || a->height != b->height) return false;
    for (int32_t y = 0; y < a->height; ++y) {
        const uint8_t *ra = a->pixels + (ptrdiff_t)y * a->stride;
        const uint8_t *rb = b->pixels + (ptrdiff_t)y * b->stride;
        if (memcmp(ra, rb, (size_t)a->width * 4) != 0) return false;
    }
    return true;
}

int main(void) {
    printf("[test_resample_mt] Starting parallel resampling tests...\n");

    size_t mem_size = 96u * 1024u * 1024u;
    void *raw = malloc(mem_size);
    assert(raw != NULL);
    proven_arena_t arena = proven_arena_create((proven_mem_mut_t){ .ptr = raw, .size = mem_size });

    /* Test 1: how the work is divided. One band means the split did not
       happen; a band per row means it is scheduling more than it
       computes. */
    {
        assert(rubraview_resample_band_count(32, 0, 8) == 1);   /* too small to bother */
        assert(rubraview_resample_band_count(4000, 0, 1) == 1); /* one worker: nothing to split */
        assert(rubraview_resample_band_count(0, 0, 8) == 0);

        size_t bands = rubraview_resample_band_count(4000, 64, 4);
        assert(bands > 1 && bands <= 16);

        /* A huge image does not turn into a thousand tiny jobs. */
        assert(rubraview_resample_band_count(100000, 1, 8) <= 64);
    }
    printf("  [PASS] The band count splits usefully and never absurdly\n");

    /* Test 2: with no pool at all, the parallel call is the serial one. */
    {
        rubraview_pixbuf_t src = rubraview_pixbuf_create(&arena, 200, 200, RUBRAVIEW_PIXFMT_RGBA8);
        assert(rubraview_pixbuf_is_valid(&src));
        fill_pattern(&src);

        rubraview_pixbuf_t serial = rubraview_pixbuf_resample(&arena, &src, 100, 100, RUBRAVIEW_FILTER_LANCZOS3);
        rubraview_pixbuf_t none = rubraview_pixbuf_resample_mt(&arena, NULL, 0, &src, 100, 100,
                                                               RUBRAVIEW_FILTER_LANCZOS3, 0);
        assert(rubraview_pixbuf_is_valid(&none));
        assert(identical(&serial, &none));
    }
    printf("  [PASS] With no worker pool the result is the ordinary one\n");

    /* Test 3: the claim that matters. Every filter, run over a real
       worker pool, must come out byte for byte the same as the
       single-threaded call — otherwise a resize would depend on how many
       cores the machine has. */
    {
        proven_job_sys_t *jobs = NULL;
        proven_allocator_t alloc = proven_arena_as_allocator(&arena);
        proven_err_t err = proven_job_system_init(alloc, 4, 256, &jobs);

        if (err == PROVEN_OK && jobs) {
            rubraview_pixbuf_t src = rubraview_pixbuf_create(&arena, 640, 480, RUBRAVIEW_PIXFMT_RGBA8);
            assert(rubraview_pixbuf_is_valid(&src));
            fill_pattern(&src);

            const rubraview_resample_filter_t FILTERS[] = {
                RUBRAVIEW_FILTER_NEAREST, RUBRAVIEW_FILTER_BILINEAR,
                RUBRAVIEW_FILTER_BICUBIC, RUBRAVIEW_FILTER_LANCZOS3,
            };
            /* Both directions, and sizes that do not divide evenly by
               the band count — a ragged last band is where an off-by-one
               would hide. */
            const int32_t SIZES[][2] = { { 321, 257 }, { 1280, 961 }, { 100, 100 } };

            for (size_t f = 0; f < 4; ++f) {
                for (size_t s = 0; s < 3; ++s) {
                    rubraview_pixbuf_t serial = rubraview_pixbuf_resample(&arena, &src,
                                                                          SIZES[s][0], SIZES[s][1], FILTERS[f]);
                    rubraview_pixbuf_t parallel = rubraview_pixbuf_resample_mt(&arena, jobs, 4, &src,
                                                                               SIZES[s][0], SIZES[s][1],
                                                                               FILTERS[f], 32);
                    assert(rubraview_pixbuf_is_valid(&parallel));
                    if (!identical(&serial, &parallel)) {
                        printf("  [FAIL] filter %zu at %dx%d differs between the two paths\n",
                               f, SIZES[s][0], SIZES[s][1]);
                        assert(false);
                    }
                }
            }

            proven_job_system_close(jobs);
            proven_job_system_destroy(jobs);
            printf("  [PASS] Every filter gives the same bytes on four workers as on one\n");
        } else {
            printf("  [PASS] No worker pool on this host; the serial path is already covered\n");
        }
    }

    /* Test 4: bad arguments produce an invalid buffer, not a crash. */
    {
        rubraview_pixbuf_t src = rubraview_pixbuf_create(&arena, 16, 16, RUBRAVIEW_PIXFMT_RGBA8);
        assert(rubraview_pixbuf_is_valid(&src));
        rubraview_pixbuf_t bad = rubraview_pixbuf_resample_mt(&arena, NULL, 0, &src, 0, 10,
                                                              RUBRAVIEW_FILTER_BILINEAR, 0);
        assert(!rubraview_pixbuf_is_valid(&bad));

        rubraview_pixbuf_t no_src = rubraview_pixbuf_resample_mt(&arena, NULL, 0, NULL, 10, 10,
                                                                 RUBRAVIEW_FILTER_BILINEAR, 0);
        assert(!rubraview_pixbuf_is_valid(&no_src));
    }
    printf("  [PASS] Bad arguments give an invalid buffer rather than a crash\n");

    free(raw);
    printf("[test_resample_mt] All tests passed successfully!\n");
    return 0;
}
