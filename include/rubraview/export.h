#ifndef RUBRAVIEW_EXPORT_H
#define RUBRAVIEW_EXPORT_H

#include "rubraview/core.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Export settings (RFC-0001 §3.10), RV-018 / RV-066.
 *
 * The encoder itself is the Windows Imaging Component's, but *what to
 * ask it for* is a decision with no Windows in it: which format, at what
 * quality, whether the metadata goes with it, and — the one that is
 * easy to get wrong — whether the file can be written without
 * re-encoding at all.
 *
 * That last one is §3.10's "zero-touch" strip: a JPEG saved with no
 * change but privacy cleaning must not be decoded and re-encoded,
 * because that loses quality for nothing. Deciding when that shortcut
 * applies is what `rubraview_export_plan` is for.
 */

typedef enum rubraview_export_format {
    RUBRAVIEW_EXPORT_SAME_AS_SOURCE = 0,
    RUBRAVIEW_EXPORT_JPEG,
    RUBRAVIEW_EXPORT_PNG,
    RUBRAVIEW_EXPORT_WEBP,
    RUBRAVIEW_EXPORT_GIF,
    RUBRAVIEW_EXPORT_BMP,
    RUBRAVIEW_EXPORT_TIFF,
    RUBRAVIEW_EXPORT_ICO,
    RUBRAVIEW_EXPORT_AVIF,   /* RV-139 (D-184): written by the user's FFmpeg (an AV1 encoder, its avif muxer) */
    RUBRAVIEW_EXPORT_JXL,    /* ...and by its libjxl */
} rubraview_export_format_t;

#define RUBRAVIEW_EXPORT_FORMATS 10   /* the choices, "as the file" first */

/** RV-139: JPEG XL's distance for a quality 1..100 (libjxl's own mapping; 100 is 0, lossless). */
double rubraview_export_jxl_distance(int32_t quality);
/** RV-139: an AV1 encoder's CRF for a quality 1..100 (100 is 0, the best it does; 1 is 63). */
int32_t rubraview_export_av1_crf(int32_t quality);

typedef enum rubraview_png_depth {
    RUBRAVIEW_PNG_RGBA32 = 0,
    RUBRAVIEW_PNG_RGB24,
    RUBRAVIEW_PNG_PALETTE8,
    RUBRAVIEW_PNG_GRAY8,
} rubraview_png_depth_t;

/* RV-140: as WIC numbers them (`WICJpegYCrCbSubsamplingOption`), 0 the writer's own (4:2:0). */
typedef enum rubraview_jpeg_subsampling {
    RUBRAVIEW_JPEG_SUBSAMPLING_AUTO = 0,
    RUBRAVIEW_JPEG_SUBSAMPLING_420,
    RUBRAVIEW_JPEG_SUBSAMPLING_422,
    RUBRAVIEW_JPEG_SUBSAMPLING_444,
} rubraview_jpeg_subsampling_t;

/* RV-140: by the level (0 none, else adaptive), as before; or one of PNG's own. */
typedef enum rubraview_png_filter {
    RUBRAVIEW_PNG_FILTER_BY_LEVEL = 0,
    RUBRAVIEW_PNG_FILTER_NONE,
    RUBRAVIEW_PNG_FILTER_SUB,
    RUBRAVIEW_PNG_FILTER_UP,
    RUBRAVIEW_PNG_FILTER_AVERAGE,
    RUBRAVIEW_PNG_FILTER_PAETH,
    RUBRAVIEW_PNG_FILTER_ADAPTIVE,
} rubraview_png_filter_t;

typedef enum rubraview_tiff_compression {
    RUBRAVIEW_TIFF_AUTO = 0,   /* the writer's own */
    RUBRAVIEW_TIFF_NONE,
    RUBRAVIEW_TIFF_LZW,
    RUBRAVIEW_TIFF_ZIP,
} rubraview_tiff_compression_t;

typedef enum rubraview_bmp_depth {
    RUBRAVIEW_BMP_32 = 0,      /* with its transparency, as before */
    RUBRAVIEW_BMP_24,
    RUBRAVIEW_BMP_8,           /* a palette of `colours` */
} rubraview_bmp_depth_t;

typedef struct rubraview_export_options {
    rubraview_export_format_t format;

    int32_t jpeg_quality;      /* 1..100 */
    bool    jpeg_progressive;

    bool    webp_lossless;
    int32_t webp_quality;      /* 1..100, ignored when lossless */
    int32_t webp_effort;       /* 0..6 */

    int32_t png_compression;   /* 0..9 */
    rubraview_png_depth_t png_depth;

    /* §3.10: the ICO writer packs several sizes into one file. */
    bool    ico_multi_size;

    bool    privacy_clean;     /* strip EXIF/GPS/XMP/IPTC */

    double  dpi;               /* RV-134: written into the file (JPEG, PNG, TIFF, BMP); 0 = as the writer does */

    /* RV-140 (owner, 2026-10-18: "jpg나 png 혹은 기타 저장 옵션들에 대해 더 다양한 옵션"):
       what the writers (WIC, libjpeg-turbo's jpegtran, libwebp) take already. */
    rubraview_jpeg_subsampling_t jpeg_subsampling;
    bool    jpeg_grey;          /* one channel: grey */
    bool    jpeg_optimise;      /* Huffman tables made for this picture: smaller, as good */
    int32_t jpeg_restart_rows;  /* 0, or a restart marker every N rows of blocks (1..64) */
    rubraview_png_filter_t png_filter;
    bool    png_interlace;      /* Adam7: shown coarse first while it loads */
    int32_t colours;            /* a palette's size, 2..256 (PNG's palette, GIF, BMP's 8 bits); 0 = 256 */
    bool    dither;             /* the colours left spread as dots, or the nearest flat (palettes) */
    rubraview_tiff_compression_t tiff_compression;
    rubraview_bmp_depth_t bmp_depth;
    int32_t webp_near_lossless; /* 0..100, lossless only: 100 is off, lower lets nearby values change for a smaller file */
    int32_t webp_alpha_quality; /* 0..100: how well the transparency is kept in a lossy WebP */
    bool    webp_sharp_yuv;     /* a slower, sharper conversion of colour edges (lossy) */

    /* 1.17.0 (owner, 2026-10-18): the source's details (EXIF, XMP, IPTC, comments) through a save that
       writes the pixels again — rubraview_keep_details_t: 0 dropped, 1 in the file (as it comes),
       2 in the file and what it cannot hold in a .txt beside it. Never with privacy_clean. */
    int32_t keep_details;

    /* 1.17.0, the rest of RV-140 that the writers take */
    int32_t webp_preset;        /* 0 none, 1 picture, 2 photo, 3 drawing, 4 icon, 5 text (libwebp's own tunings) */
    int32_t target_kb;          /* JPEG and lossy WebP: the file no larger than this many KB (the quality found for it); 0 off */
    bool    tiff_cmyk;          /* a TIFF in CMYK, for print (WIC's plain conversion, no colour profile) */

    /* 1.31.0 (RV-139, D-185): AVIF's colour, written by FFmpeg */
    int32_t avif_chroma;        /* 0 4:2:0, 1 4:4:4 (libaom, rav1e; SVT-AV1 writes 4:2:0) */
    int32_t avif_bits;          /* 8 or 10 */
} rubraview_export_options_t;

/** Every field at the specification's default. */
rubraview_export_options_t rubraview_export_defaults(void);

/** Clamp every field into its legal range; call after reading settings.ini. */
void rubraview_export_clamp(rubraview_export_options_t *options);

/** The file extension a format writes, without the dot ("jpg", "png", ...). */
u8str_t rubraview_export_extension(rubraview_export_format_t format);

/**
 * RV-134: a JPEG's JFIF block (APP0 right after SOI) set to `dpi` dots an
 * inch, in place: the picture itself is not touched. False when the file
 * has no JFIF block there (the caller then writes it another way).
 */
bool rubraview_jpeg_set_dpi(uint8_t *jpeg, size_t len, double dpi);

/** The format a filename implies, or SAME_AS_SOURCE when it names none. */
rubraview_export_format_t rubraview_export_format_for_name(u8str_t filename);

/* The export panel's rows a format has use for (owner, 2026-10-15: only those shown). */
enum {
    RUBRAVIEW_EXPORT_ROW_QUALITY     = 1u << 0,   /* JPEG, and WebP when not lossless */
    RUBRAVIEW_EXPORT_ROW_PROGRESSIVE = 1u << 1,   /* JPEG */
    RUBRAVIEW_EXPORT_ROW_PNG_LEVEL   = 1u << 2,   /* PNG */
    RUBRAVIEW_EXPORT_ROW_PNG_DEPTH   = 1u << 3,   /* PNG */
    RUBRAVIEW_EXPORT_ROW_WEBP        = 1u << 4,   /* WebP: lossless and effort */
    RUBRAVIEW_EXPORT_ROW_PRIVACY     = 1u << 5,   /* a format that carries EXIF or XMP: JPEG, PNG, WebP, TIFF */
    /* RV-140 */
    RUBRAVIEW_EXPORT_ROW_JPEG_MORE   = 1u << 6,   /* JPEG: subsampling, grey, optimised tables, restart markers */
    RUBRAVIEW_EXPORT_ROW_PNG_MORE    = 1u << 7,   /* PNG: filter, interlace */
    RUBRAVIEW_EXPORT_ROW_COLOURS     = 1u << 8,   /* a palette: PNG's palette, GIF, BMP's 8 bits — its size and dithering */
    RUBRAVIEW_EXPORT_ROW_TIFF        = 1u << 9,   /* TIFF: compression */
    RUBRAVIEW_EXPORT_ROW_BMP         = 1u << 10,  /* BMP: bits */
    RUBRAVIEW_EXPORT_ROW_WEBP_LOSSY  = 1u << 11,  /* lossy WebP: alpha quality, sharp YUV */
    RUBRAVIEW_EXPORT_ROW_WEBP_NEAR   = 1u << 12,  /* lossless WebP: near-lossless */
    RUBRAVIEW_EXPORT_ROW_TARGET      = 1u << 13,  /* JPEG and lossy WebP: a target size (1.17.0) */
    RUBRAVIEW_EXPORT_ROW_AVIF        = 1u << 14,  /* AVIF: 4:2:0 or 4:4:4, 8 or 10 bits (1.31.0) */
};

/**
 * Which rows the export panel shows for `format`; "as the file" stands
 * for the format `source_name`'s extension names (none known: no rows).
 */
uint32_t rubraview_export_rows(rubraview_export_format_t format, u8str_t source_name, bool webp_lossless);

/** RV-140: the rows for these options, the palette's only when one is written. */
uint32_t rubraview_export_rows_of(const rubraview_export_options_t *options, u8str_t source_name);

/** RV-140: the palette's size these options ask for, 2..256. */
int32_t rubraview_export_palette_size(const rubraview_export_options_t *options);

/** The sizes §3.10's multi-size ICO packs. Returns how many were written. */
size_t rubraview_export_ico_sizes(int32_t *out_sizes, size_t capacity);

typedef enum rubraview_export_route {
    /** Decode, apply, re-encode: the ordinary path. */
    RUBRAVIEW_EXPORT_REENCODE = 0,
    /**
     * §3.10's zero-touch path: copy the original bytes and excise the
     * metadata markers. Only when nothing else about the image changes,
     * and only for a container whose markers can be cut out — JPEG.
     */
    RUBRAVIEW_EXPORT_STRIP_ONLY,
    /** Nothing to do at all: copy the file unchanged. */
    RUBRAVIEW_EXPORT_COPY,
    /**
     * 1.17.0: a JPEG to a JPEG, nothing changed, but progressive, optimised
     * tables or restart markers asked for: its coefficients written again
     * (jpegtran), lossless; its markers kept unless privacy clean.
     */
    RUBRAVIEW_EXPORT_JPEG_LOSSLESS,
} rubraview_export_route_t;

/**
 * Decide how a save should be carried out. `pixels_changed` is the
 * caller's answer to "did an edit, a resize or a rotation touch the
 * image?" — the export layer does not guess it.
 *
 * The rule this encodes, in one sentence: re-encoding a photograph
 * costs quality, so it is only done when something actually asked for
 * it.
 */
rubraview_export_route_t rubraview_export_plan(const rubraview_export_options_t *options,
                                               rubraview_export_format_t source_format,
                                               bool pixels_changed);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_EXPORT_H */
