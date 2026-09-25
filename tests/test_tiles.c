#include "rubraview/tiles.h"
#include <stdio.h>
#include <assert.h>
#include <math.h>

static bool approx(double a, double b) { return fabs(a - b) < 1e-9; }

int main(void) {
    printf("[test_tiles] Starting tile unit tests...\n");

    /* Tiles are wanted only when the reduced texture is magnified. */
    {
        assert(!rubraview_tiles_wanted(0.05, 20000, 11585));   /* fitted: the texture is shrunk */
        assert(rubraview_tiles_wanted(1.0, 20000, 11585));     /* actual size: each texel ~1.7 px */
        assert(!rubraview_tiles_wanted(1.0, 4000, 4000));      /* not reduced */
        assert(!rubraview_tiles_wanted(0.58, 20000, 11585));   /* 1.001: within the 5 % */
    }
    printf("  [PASS] Tiles are wanted only where the reduced texture is magnified\n");

    /* The level is the coarsest still at least as sharp as the screen. */
    {
        assert(rubraview_tile_level(1.0) == 0);
        assert(rubraview_tile_level(3.0) == 0);
        assert(rubraview_tile_level(0.5) == 1);
        assert(rubraview_tile_level(0.49) == 1);   /* 2^-1 = 0.5 >= 0.49 */
        assert(rubraview_tile_level(0.26) == 1);
        assert(rubraview_tile_level(0.25) == 2);
        assert(rubraview_tile_level(1e-9) == RUBRAVIEW_TILE_MAX_LEVEL);
        assert(rubraview_tile_level(0.0) == 0);
    }
    printf("  [PASS] The level is the coarsest at least as sharp as the screen\n");

    /* A tile's rectangle, clipped at the picture's edge, and its output size. */
    {
        int32_t x, y, w, h, ow, oh;
        assert(rubraview_tile_geometry((rubraview_tile_key_t){ 0, 1, 2 }, 2000, 1300, &x, &y, &w, &h, &ow, &oh));
        assert(x == 512 && y == 1024 && w == 512 && h == 276 && ow == 512 && oh == 276);
        assert(rubraview_tile_geometry((rubraview_tile_key_t){ 2, 9, 5 }, 20000, 12000, &x, &y, &w, &h, &ow, &oh));
        assert(x == 18432 && y == 10240 && w == 1568 && h == 1760 && ow == 392 && oh == 440);
        assert(!rubraview_tile_geometry((rubraview_tile_key_t){ 0, 4, 0 }, 2000, 1300, &x, &y, &w, &h, &ow, &oh));
        assert(!rubraview_tile_geometry((rubraview_tile_key_t){ -1, 0, 0 }, 2000, 1300, &x, &y, &w, &h, &ow, &oh));
    }
    printf("  [PASS] A tile's rectangle is clipped to the picture, its output rounded up\n");

    /* The tiles a view meets, the middle first, clipped to the picture. */
    {
        rubraview_tile_key_t keys[64];
        size_t n = rubraview_tiles_visible(600, 600, 1900, 1400, 0, 20000, 12000, keys, 64);
        assert(n == 6);                                   /* columns 1-3, rows 1-2 */
        assert(keys[0].tx == 2 && keys[0].ty == 1);       /* nearest the centre (1250,1000) */
        for (size_t i = 0; i < n; ++i) {
            assert(keys[i].level == 0 && keys[i].tx >= 1 && keys[i].tx <= 3 && keys[i].ty >= 1 && keys[i].ty <= 2);
        }

        /* Past the picture's edge nothing is asked for. */
        n = rubraview_tiles_visible(-5000, -5000, 300, 300, 0, 2000, 1300, keys, 64);
        assert(n == 1 && keys[0].tx == 0 && keys[0].ty == 0);
        n = rubraview_tiles_visible(3000, 0, 4000, 100, 0, 2000, 1300, keys, 64);
        assert(n == 0);

        /* A coarser level covers more with fewer; the cap holds. */
        n = rubraview_tiles_visible(0, 0, 20000, 12000, 3, 20000, 12000, keys, 64);
        assert(n == 5 * 3);                               /* 4096-pixel tiles */
        n = rubraview_tiles_visible(0, 0, 20000, 12000, 0, 20000, 12000, keys, 10);
        assert(n == 10);
    }
    printf("  [PASS] The tiles a view meets are listed middle first and clipped to the picture\n");

    /* The inverse undoes the matrix. */
    {
        rubraview_mat3x2_t m = rubraview_mat3x2_multiply(rubraview_mat3x2_scale(0.25, 0.5),
                                                         rubraview_mat3x2_translate(40.0, -12.0));
        rubraview_mat3x2_t inv;
        assert(rubraview_mat3x2_invert(m, &inv));
        double x, y, bx, by;
        rubraview_mat3x2_apply(m, 123.0, 456.0, &x, &y);
        rubraview_mat3x2_apply(inv, x, y, &bx, &by);
        assert(approx(bx, 123.0) && approx(by, 456.0));
        assert(!rubraview_mat3x2_invert(rubraview_mat3x2_scale(0.0, 1.0), &inv));
    }
    printf("  [PASS] mat3x2_invert undoes the matrix; a flat one has none\n");

    printf("[test_tiles] All tests passed successfully!\n");
    return 0;
}
