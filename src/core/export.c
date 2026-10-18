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
        .jpeg_subsampling = RUBRAVIEW_JPEG_SUBSAMPLING_AUTO,
        .png_filter = RUBRAVIEW_PNG_FILTER_BY_LEVEL,
        .colours = 256,
        .dither = true,              /* as WIC's own palettes were made before */
        .tiff_compression = RUBRAVIEW_TIFF_AUTO,
        .bmp_depth = RUBRAVIEW_BMP_32,
        .webp_near_lossless = 100,   /* off */
        .webp_alpha_quality = 100,
    };
}

void rubraview_export_clamp(rubraview_export_options_t *options) {
    if (!options) return;
    options->jpeg_quality = clampi(options->jpeg_quality, 1, 100);
    options->webp_quality = clampi(options->webp_quality, 1, 100);
    options->webp_effort = clampi(options->webp_effort, 0, 6);
    options->png_compression = clampi(options->png_compression, 0, 9);
    if (!(options->dpi >= 1.0 && options->dpi <= 10000.0)) options->dpi = 0.0;   /* RV-134: none, or a sane one */
    if ((int)options->format < 0 || (int)options->format > RUBRAVIEW_EXPORT_ICO) {
        options->format = RUBRAVIEW_EXPORT_SAME_AS_SOURCE;
    }
    if ((int)options->png_depth < 0 || (int)options->png_depth > RUBRAVIEW_PNG_GRAY8) {
        options->png_depth = RUBRAVIEW_PNG_RGBA32;
    }
    /* RV-140 */
    if ((int)options->jpeg_subsampling < 0 || (int)options->jpeg_subsampling > RUBRAVIEW_JPEG_SUBSAMPLING_444) {
        options->jpeg_subsampling = RUBRAVIEW_JPEG_SUBSAMPLING_AUTO;
    }
    options->jpeg_restart_rows = clampi(options->jpeg_restart_rows, 0, 64);
    if ((int)options->png_filter < 0 || (int)options->png_filter > RUBRAVIEW_PNG_FILTER_ADAPTIVE) {
        options->png_filter = RUBRAVIEW_PNG_FILTER_BY_LEVEL;
    }
    options->colours = options->colours == 0 ? 256 : clampi(options->colours, 2, 256);
    if ((int)options->tiff_compression < 0 || (int)options->tiff_compression > RUBRAVIEW_TIFF_ZIP) {
        options->tiff_compression = RUBRAVIEW_TIFF_AUTO;
    }
    if ((int)options->bmp_depth < 0 || (int)options->bmp_depth > RUBRAVIEW_BMP_8) options->bmp_depth = RUBRAVIEW_BMP_32;
    options->webp_near_lossless = clampi(options->webp_near_lossless, 0, 100);
    options->webp_alpha_quality = clampi(options->webp_alpha_quality, 0, 100);
}

int32_t rubraview_export_palette_size(const rubraview_export_options_t *options) {
    if (!options || options->colours == 0) return 256;
    return clampi(options->colours, 2, 256);
}

uint32_t rubraview_export_rows_of(const rubraview_export_options_t *o, u8str_t source_name) {
    if (!o) return 0u;
    uint32_t rows = rubraview_export_rows(o->format, source_name, o->webp_lossless);
    rubraview_export_format_t format = o->format == RUBRAVIEW_EXPORT_SAME_AS_SOURCE
                                         ? rubraview_export_format_for_name(source_name) : o->format;
    switch (format) {
        case RUBRAVIEW_EXPORT_JPEG: rows |= RUBRAVIEW_EXPORT_ROW_JPEG_MORE; break;
        case RUBRAVIEW_EXPORT_PNG:
            rows |= RUBRAVIEW_EXPORT_ROW_PNG_MORE;
            if (o->png_depth == RUBRAVIEW_PNG_PALETTE8) rows |= RUBRAVIEW_EXPORT_ROW_COLOURS;
            break;
        case RUBRAVIEW_EXPORT_GIF:  rows |= RUBRAVIEW_EXPORT_ROW_COLOURS; break;
        case RUBRAVIEW_EXPORT_BMP:
            rows |= RUBRAVIEW_EXPORT_ROW_BMP;
            if (o->bmp_depth == RUBRAVIEW_BMP_8) rows |= RUBRAVIEW_EXPORT_ROW_COLOURS;
            break;
        case RUBRAVIEW_EXPORT_TIFF: rows |= RUBRAVIEW_EXPORT_ROW_TIFF; break;
        case RUBRAVIEW_EXPORT_WEBP: rows |= o->webp_lossless ? RUBRAVIEW_EXPORT_ROW_WEBP_NEAR : RUBRAVIEW_EXPORT_ROW_WEBP_LOSSY; break;
        default: break;
    }
    return rows;
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
    if (rubraview_u8_eq_lit_ci(ext, "png") || rubraview_u8_eq_lit_ci(ext, "apng")) return RUBRAVIEW_EXPORT_PNG;
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

uint32_t rubraview_export_rows(rubraview_export_format_t format, u8str_t source_name, bool webp_lossless) {
    if (format == RUBRAVIEW_EXPORT_SAME_AS_SOURCE) format = rubraview_export_format_for_name(source_name);
    switch (format) {
        case RUBRAVIEW_EXPORT_JPEG:
            return RUBRAVIEW_EXPORT_ROW_QUALITY | RUBRAVIEW_EXPORT_ROW_PROGRESSIVE | RUBRAVIEW_EXPORT_ROW_PRIVACY;
        case RUBRAVIEW_EXPORT_PNG:
            return RUBRAVIEW_EXPORT_ROW_PNG_LEVEL | RUBRAVIEW_EXPORT_ROW_PNG_DEPTH | RUBRAVIEW_EXPORT_ROW_PRIVACY;
        case RUBRAVIEW_EXPORT_WEBP:
            return (webp_lossless ? 0u : (uint32_t)RUBRAVIEW_EXPORT_ROW_QUALITY) | RUBRAVIEW_EXPORT_ROW_WEBP | RUBRAVIEW_EXPORT_ROW_PRIVACY;
        case RUBRAVIEW_EXPORT_TIFF:
            return RUBRAVIEW_EXPORT_ROW_PRIVACY;
        default:
            return 0u;   /* GIF, BMP, ICO: nothing to choose */
    }
}

bool rubraview_jpeg_set_dpi(uint8_t *j, size_t len, double dpi) {
    if (!j || len < 20 || !(dpi >= 1.0 && dpi <= 65535.0)) return false;
    if (j[0] != 0xFF || j[1] != 0xD8 || j[2] != 0xFF || j[3] != 0xE0) return false;
    size_t seg = ((size_t)j[4] << 8) | j[5];
    if (seg < 16 || 4 + seg > len || memcmp(j + 6, "JFIF\0", 5) != 0) return false;
    unsigned d = (unsigned)(dpi + 0.5);
    j[13] = 1;                                          /* units: dots an inch */
    j[14] = (uint8_t)(d >> 8); j[15] = (uint8_t)d;      /* X density */
    j[16] = (uint8_t)(d >> 8); j[17] = (uint8_t)d;      /* Y density */
    return true;
}
