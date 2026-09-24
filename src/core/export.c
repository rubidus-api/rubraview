#include "rubraview/export.h"
#include "rubraview/path.h"
#include <string.h>

static int32_t clampi(int32_t v, int32_t lo, int32_t hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

rubraview_export_options_t rubraview_export_defaults(void) {
    return (rubraview_export_options_t){
        .format = RUBRAVIEW_EXPORT_SAME_AS_SOURCE,
        .jpeg_quality = 90,
        .jpeg_progressive = false,
        .webp_lossless = false,
        .webp_quality = 85,
        .webp_effort = 4,
        .png_compression = 6,
        .png_depth = RUBRAVIEW_PNG_RGBA32,
        .ico_multi_size = true,
        /* Off by default: silently discarding a photographer's metadata
           would be its own kind of surprise. §3.10 makes it an explicit
           toggle, so it starts unticked. */
        .privacy_clean = false,
    };
}

void rubraview_export_clamp(rubraview_export_options_t *options) {
    if (!options) return;
    options->jpeg_quality = clampi(options->jpeg_quality, 1, 100);
    options->webp_quality = clampi(options->webp_quality, 1, 100);
    options->webp_effort = clampi(options->webp_effort, 0, 6);
    options->png_compression = clampi(options->png_compression, 0, 9);
    if ((int)options->format < 0 || (int)options->format > RUBRAVIEW_EXPORT_ICO) {
        options->format = RUBRAVIEW_EXPORT_SAME_AS_SOURCE;
    }
    if ((int)options->png_depth < 0 || (int)options->png_depth > RUBRAVIEW_PNG_GRAY8) {
        options->png_depth = RUBRAVIEW_PNG_RGBA32;
    }
}

u8str_t rubraview_export_extension(rubraview_export_format_t format) {
    switch (format) {
        case RUBRAVIEW_EXPORT_JPEG: return U8("jpg");
        case RUBRAVIEW_EXPORT_PNG:  return U8("png");
        case RUBRAVIEW_EXPORT_WEBP: return U8("webp");
        case RUBRAVIEW_EXPORT_GIF:  return U8("gif");
        case RUBRAVIEW_EXPORT_BMP:  return U8("bmp");
        case RUBRAVIEW_EXPORT_TIFF: return U8("tif");
        case RUBRAVIEW_EXPORT_ICO:  return U8("ico");
        case RUBRAVIEW_EXPORT_SAME_AS_SOURCE:
        default: return (u8str_t){ .ptr = "", .len = 0 };
    }
}

rubraview_export_format_t rubraview_export_format_for_name(u8str_t filename) {
    u8str_t ext = rubraview_path_ext(filename);
    if (ext.len > 0 && ext.ptr[0] == '.') { ext.ptr++; ext.len--; }

    if (rubraview_u8_eq_lit_ci(ext, "jpg") || rubraview_u8_eq_lit_ci(ext, "jpeg") || rubraview_u8_eq_lit_ci(ext, "jpe")) return RUBRAVIEW_EXPORT_JPEG;
    if (rubraview_u8_eq_lit_ci(ext, "png")) return RUBRAVIEW_EXPORT_PNG;
    if (rubraview_u8_eq_lit_ci(ext, "webp")) return RUBRAVIEW_EXPORT_WEBP;
    if (rubraview_u8_eq_lit_ci(ext, "gif")) return RUBRAVIEW_EXPORT_GIF;
    if (rubraview_u8_eq_lit_ci(ext, "bmp")) return RUBRAVIEW_EXPORT_BMP;
    if (rubraview_u8_eq_lit_ci(ext, "tif") || rubraview_u8_eq_lit_ci(ext, "tiff")) return RUBRAVIEW_EXPORT_TIFF;
    if (rubraview_u8_eq_lit_ci(ext, "ico")) return RUBRAVIEW_EXPORT_ICO;
    return RUBRAVIEW_EXPORT_SAME_AS_SOURCE;
}

size_t rubraview_export_ico_sizes(int32_t *out_sizes, size_t capacity) {
    static const int32_t SIZES[] = { 16, 32, 48, 256 };  /* §3.10 */
    size_t count = sizeof(SIZES) / sizeof(SIZES[0]);
    if (!out_sizes) return count;
    for (size_t i = 0; i < count && i < capacity; ++i) out_sizes[i] = SIZES[i];
    return count;
}

rubraview_export_route_t rubraview_export_plan(const rubraview_export_options_t *options,
                                               rubraview_export_format_t source_format,
                                               bool pixels_changed) {
    if (!options) return RUBRAVIEW_EXPORT_REENCODE;

    rubraview_export_format_t target = options->format == RUBRAVIEW_EXPORT_SAME_AS_SOURCE
                                         ? source_format : options->format;

    /* A different container always means encoding again. */
    if (target != source_format) return RUBRAVIEW_EXPORT_REENCODE;
    if (pixels_changed) return RUBRAVIEW_EXPORT_REENCODE;

    if (!options->privacy_clean) return RUBRAVIEW_EXPORT_COPY;

    /* §3.10's zero-touch strip. It is written for JPEG's marker
       structure — APP1, XMP and IPTC segments can be cut out of the
       stream without touching a single DCT coefficient. The other
       containers store metadata in ways that a byte-level excision
       cannot do safely, so they take the honest slow path. */
    if (source_format == RUBRAVIEW_EXPORT_JPEG) return RUBRAVIEW_EXPORT_STRIP_ONLY;

    return RUBRAVIEW_EXPORT_REENCODE;
}
