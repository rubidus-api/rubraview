#include "rubraview/export.h"
#include "rubraview/path.h"
#include <string.h>
#include <math.h>

static int32_t clampi(int32_t v, int32_t lo, int32_t hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

rubraview_export_options_t rubraview_export_defaults(void) {
    return (rubraview_export_options_t){
        .format = RUBRAVIEW_EXPORT_SAME_AS_SOURCE,
        .jpeg_quality = 90,
        .avif_bits = 8,   /* 1.31.0 */
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
        .keep_details = 1,           /* in the file */
    };
}

void rubraview_export_clamp(rubraview_export_options_t *options) {
    if (!options) return;
    options->jpeg_quality = clampi(options->jpeg_quality, 1, 100);
    options->webp_quality = clampi(options->webp_quality, 1, 100);
    options->webp_effort = clampi(options->webp_effort, 0, 6);
    options->png_compression = clampi(options->png_compression, 0, 9);
    if (!(options->dpi >= 1.0 && options->dpi <= 10000.0)) options->dpi = 0.0;   /* RV-134: none, or a sane one */
    if ((int)options->format < 0 || (int)options->format >= RUBRAVIEW_EXPORT_FORMATS) {
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
    options->keep_details = clampi(options->keep_details, 0, 2);
    options->webp_preset = clampi(options->webp_preset, 0, 5);
    options->target_kb = clampi(options->target_kb, 0, 1000000);
    options->avif_chroma = clampi(options->avif_chroma, 0, 1);
    options->avif_bits = options->avif_bits == 10 ? 10 : 8;
}

int32_t rubraview_export_palette_size(const rubraview_export_options_t *options) {
    if (!options || options->colours == 0) return 256;
    return clampi(options->colours, 2, 256);
}

uint32_t rubraview_export_rows_of(const rubraview_export_options_t *o, proven_u8str_view_t source_name) {
    if (!o) return 0u;
    uint32_t rows = rubraview_export_rows(o->format, source_name, o->webp_lossless);
    rubraview_export_format_t format = o->format == RUBRAVIEW_EXPORT_SAME_AS_SOURCE
                                         ? rubraview_export_format_for_name(source_name) : o->format;
    switch (format) {
        case RUBRAVIEW_EXPORT_JPEG: rows |= RUBRAVIEW_EXPORT_ROW_JPEG_MORE | RUBRAVIEW_EXPORT_ROW_TARGET; break;
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
        case RUBRAVIEW_EXPORT_AVIF: rows |= RUBRAVIEW_EXPORT_ROW_AVIF; break;   /* 1.31.0 */
        case RUBRAVIEW_EXPORT_WEBP:
            rows |= o->webp_lossless ? RUBRAVIEW_EXPORT_ROW_WEBP_NEAR : (RUBRAVIEW_EXPORT_ROW_WEBP_LOSSY | RUBRAVIEW_EXPORT_ROW_TARGET);
            break;
        default: break;
    }
    return rows;
}

proven_u8str_view_t rubraview_export_extension(rubraview_export_format_t format) {
    switch (format) {
        case RUBRAVIEW_EXPORT_JPEG: return PROVEN_LIT("jpg");
        case RUBRAVIEW_EXPORT_PNG:  return PROVEN_LIT("png");
        case RUBRAVIEW_EXPORT_WEBP: return PROVEN_LIT("webp");
        case RUBRAVIEW_EXPORT_GIF:  return PROVEN_LIT("gif");
        case RUBRAVIEW_EXPORT_BMP:  return PROVEN_LIT("bmp");
        case RUBRAVIEW_EXPORT_TIFF: return PROVEN_LIT("tif");
        case RUBRAVIEW_EXPORT_ICO:  return PROVEN_LIT("ico");
        case RUBRAVIEW_EXPORT_AVIF: return PROVEN_LIT("avif");
        case RUBRAVIEW_EXPORT_JXL:  return PROVEN_LIT("jxl");
        case RUBRAVIEW_EXPORT_SAME_AS_SOURCE:
        default: return PROVEN_LIT("");
    }
}

rubraview_export_format_t rubraview_export_format_for_name(proven_u8str_view_t filename) {
    proven_u8str_view_t ext = rubraview_path_ext(filename);
    if (ext.size > 0 && ext.ptr[0] == '.') { ext.ptr++; ext.size--; }

    if (proven_u8str_view_eq_ascii_ci(ext, PROVEN_LIT("jpg")) || proven_u8str_view_eq_ascii_ci(ext, PROVEN_LIT("jpeg")) || proven_u8str_view_eq_ascii_ci(ext, PROVEN_LIT("jpe"))) return RUBRAVIEW_EXPORT_JPEG;
    if (proven_u8str_view_eq_ascii_ci(ext, PROVEN_LIT("png")) || proven_u8str_view_eq_ascii_ci(ext, PROVEN_LIT("apng"))) return RUBRAVIEW_EXPORT_PNG;
    if (proven_u8str_view_eq_ascii_ci(ext, PROVEN_LIT("webp"))) return RUBRAVIEW_EXPORT_WEBP;
    if (proven_u8str_view_eq_ascii_ci(ext, PROVEN_LIT("gif"))) return RUBRAVIEW_EXPORT_GIF;
    if (proven_u8str_view_eq_ascii_ci(ext, PROVEN_LIT("bmp"))) return RUBRAVIEW_EXPORT_BMP;
    if (proven_u8str_view_eq_ascii_ci(ext, PROVEN_LIT("tif")) || proven_u8str_view_eq_ascii_ci(ext, PROVEN_LIT("tiff"))) return RUBRAVIEW_EXPORT_TIFF;
    if (proven_u8str_view_eq_ascii_ci(ext, PROVEN_LIT("ico"))) return RUBRAVIEW_EXPORT_ICO;
    if (proven_u8str_view_eq_ascii_ci(ext, PROVEN_LIT("avif"))) return RUBRAVIEW_EXPORT_AVIF;
    if (proven_u8str_view_eq_ascii_ci(ext, PROVEN_LIT("jxl"))) return RUBRAVIEW_EXPORT_JXL;
    return RUBRAVIEW_EXPORT_SAME_AS_SOURCE;
}

double rubraview_export_jxl_distance(int32_t quality) {
    double q = quality < 1 ? 1.0 : quality > 100 ? 100.0 : (double)quality;
    if (q >= 100.0) return 0.0;
    return q >= 30.0 ? 0.1 + (100.0 - q) * 0.09 : 53.0 / 3000.0 * q * q - 23.0 / 20.0 * q + 25.0;
}

int32_t rubraview_export_av1_crf(int32_t quality) {
    int32_t q = quality < 1 ? 1 : quality > 100 ? 100 : quality;
    return (int32_t)lround((100.0 - (double)q) * 63.0 / 99.0);
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

    /* 1.17.0: a choice the file would not show unless it were written again
       (a copy used to drop it without a word): written again. A JPEG's scans,
       tables and restart markers are rewritten from its coefficients, lossless. */
    rubraview_export_options_t d = rubraview_export_defaults();
    switch (target) {
        case RUBRAVIEW_EXPORT_JPEG:
            if (options->jpeg_grey || options->jpeg_subsampling != RUBRAVIEW_JPEG_SUBSAMPLING_AUTO || options->target_kb > 0) return RUBRAVIEW_EXPORT_REENCODE;
            if (options->jpeg_progressive || options->jpeg_optimise || options->jpeg_restart_rows > 0) return RUBRAVIEW_EXPORT_JPEG_LOSSLESS;
            break;
        case RUBRAVIEW_EXPORT_PNG:
            if (options->png_depth != d.png_depth || options->png_interlace || options->png_filter != d.png_filter ||
                (options->png_depth == RUBRAVIEW_PNG_PALETTE8 && (options->colours != d.colours || options->dither != d.dither))) return RUBRAVIEW_EXPORT_REENCODE;
            break;
        case RUBRAVIEW_EXPORT_GIF:
            if (options->colours != d.colours || options->dither != d.dither) return RUBRAVIEW_EXPORT_REENCODE;
            break;
        case RUBRAVIEW_EXPORT_WEBP:
            if (options->target_kb > 0 && !options->webp_lossless) return RUBRAVIEW_EXPORT_REENCODE;
            break;
        case RUBRAVIEW_EXPORT_BMP:
            if (options->bmp_depth != d.bmp_depth) return RUBRAVIEW_EXPORT_REENCODE;
            break;
        case RUBRAVIEW_EXPORT_TIFF:
            if (options->tiff_compression != d.tiff_compression || options->tiff_cmyk) return RUBRAVIEW_EXPORT_REENCODE;
            break;
        default: break;
    }

    if (!options->privacy_clean) return RUBRAVIEW_EXPORT_COPY;

    /* §3.10's zero-touch strip. It is written for JPEG's marker
       structure — APP1, XMP and IPTC segments can be cut out of the
       stream without touching a single DCT coefficient. The other
       containers store metadata in ways that a byte-level excision
       cannot do safely, so they take the honest slow path. */
    if (source_format == RUBRAVIEW_EXPORT_JPEG) return RUBRAVIEW_EXPORT_STRIP_ONLY;

    return RUBRAVIEW_EXPORT_REENCODE;
}

uint32_t rubraview_export_rows(rubraview_export_format_t format, proven_u8str_view_t source_name, bool webp_lossless) {
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
        case RUBRAVIEW_EXPORT_AVIF:
        case RUBRAVIEW_EXPORT_JXL:
            return RUBRAVIEW_EXPORT_ROW_QUALITY | RUBRAVIEW_EXPORT_ROW_PRIVACY;   /* RV-139; 1.31.0 carries details */
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
