#include "rubraview/export.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

static u8str_t lit(const char *s) { return (u8str_t){ .ptr = s, .len = strlen(s) }; }
static bool is(u8str_t s, const char *l) { return s.len == strlen(l) && memcmp(s.ptr, l, s.len) == 0; }

int main(void) {
    printf("[test_export] Starting export option unit tests...\n");

    /* Test 1: defaults are within range and privacy cleaning starts off
       — throwing a photographer's metadata away has to be asked for. */
    {
        rubraview_export_options_t o = rubraview_export_defaults();
        assert(o.jpeg_quality >= 1 && o.jpeg_quality <= 100);
        assert(o.png_compression >= 0 && o.png_compression <= 9);
        assert(!o.privacy_clean);
        assert(o.format == RUBRAVIEW_EXPORT_SAME_AS_SOURCE);
    }
    printf("  [PASS] The defaults are legal and privacy cleaning is opt-in\n");

    /* Test 2: a settings file with nonsense in it is clamped, not
       obeyed. */
    {
        rubraview_export_options_t o = rubraview_export_defaults();
        o.jpeg_quality = 5000; o.webp_quality = -3; o.png_compression = 99; o.webp_effort = 42;
        o.format = (rubraview_export_format_t)999;
        o.png_depth = (rubraview_png_depth_t)-7;
        rubraview_export_clamp(&o);
        assert(o.jpeg_quality == 100 && o.webp_quality == 1);
        assert(o.png_compression == 9 && o.webp_effort == 6);
        assert(o.format == RUBRAVIEW_EXPORT_SAME_AS_SOURCE);
        assert(o.png_depth == RUBRAVIEW_PNG_RGBA32);
    }
    printf("  [PASS] Out-of-range settings are clamped rather than obeyed\n");

    /* Test 3: names and formats agree in both directions. */
    {
        assert(rubraview_export_format_for_name(lit("photo.JPG")) == RUBRAVIEW_EXPORT_JPEG);
        assert(rubraview_export_format_for_name(lit("photo.jpeg")) == RUBRAVIEW_EXPORT_JPEG);
        assert(rubraview_export_format_for_name(lit("a/b/c.webp")) == RUBRAVIEW_EXPORT_WEBP);
        assert(rubraview_export_format_for_name(lit("scan.tiff")) == RUBRAVIEW_EXPORT_TIFF);
        assert(rubraview_export_format_for_name(lit("README")) == RUBRAVIEW_EXPORT_SAME_AS_SOURCE);
        assert(rubraview_export_format_for_name(lit("archive.tar.gz")) == RUBRAVIEW_EXPORT_SAME_AS_SOURCE);

        assert(is(rubraview_export_extension(RUBRAVIEW_EXPORT_PNG), "png"));
        assert(is(rubraview_export_extension(RUBRAVIEW_EXPORT_ICO), "ico"));
        assert(rubraview_export_extension(RUBRAVIEW_EXPORT_SAME_AS_SOURCE).len == 0);
    }
    printf("  [PASS] A filename and a format name the same thing\n");

    /* Test 4: §3.10's multi-size ICO sizes. */
    {
        int32_t sizes[8] = {0};
        size_t n = rubraview_export_ico_sizes(sizes, 8);
        assert(n == 4);
        assert(sizes[0] == 16 && sizes[1] == 32 && sizes[2] == 48 && sizes[3] == 256);
        assert(rubraview_export_ico_sizes(NULL, 0) == 4);
    }
    printf("  [PASS] The ICO writer packs 16, 32, 48 and 256\n");

    /* Test 5: §3.10's zero-touch rule. This is the one that decides
       whether a photograph is re-encoded, so each case is spelled out. */
    {
        rubraview_export_options_t o = rubraview_export_defaults();

        /* Nothing asked for: copy the file. */
        assert(rubraview_export_plan(&o, RUBRAVIEW_EXPORT_JPEG, false) == RUBRAVIEW_EXPORT_COPY);

        /* Only privacy cleaning, and it is a JPEG: cut the markers out
           and leave the coefficients alone. */
        o.privacy_clean = true;
        assert(rubraview_export_plan(&o, RUBRAVIEW_EXPORT_JPEG, false) == RUBRAVIEW_EXPORT_STRIP_ONLY);

        /* Same, but the pixels changed: there is no way round encoding. */
        assert(rubraview_export_plan(&o, RUBRAVIEW_EXPORT_JPEG, true) == RUBRAVIEW_EXPORT_REENCODE);

        /* A PNG cannot be cleaned by cutting bytes out safely. */
        assert(rubraview_export_plan(&o, RUBRAVIEW_EXPORT_PNG, false) == RUBRAVIEW_EXPORT_REENCODE);

        /* A different target container always re-encodes. */
        o.privacy_clean = false;
        o.format = RUBRAVIEW_EXPORT_PNG;
        assert(rubraview_export_plan(&o, RUBRAVIEW_EXPORT_JPEG, false) == RUBRAVIEW_EXPORT_REENCODE);

        /* "Same as source" against the same source is not a change. */
        o.format = RUBRAVIEW_EXPORT_SAME_AS_SOURCE;
        assert(rubraview_export_plan(&o, RUBRAVIEW_EXPORT_PNG, false) == RUBRAVIEW_EXPORT_COPY);

        /* Naming the source's own format explicitly is the same thing. */
        o.format = RUBRAVIEW_EXPORT_PNG;
        assert(rubraview_export_plan(&o, RUBRAVIEW_EXPORT_PNG, false) == RUBRAVIEW_EXPORT_COPY);
    }
    printf("  [PASS] A photograph is only re-encoded when something asked for it\n");

    printf("[test_export] All tests passed successfully!\n");
    return 0;
}
