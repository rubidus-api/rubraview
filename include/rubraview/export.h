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
} rubraview_export_format_t;

typedef enum rubraview_png_depth {
    RUBRAVIEW_PNG_RGBA32 = 0,
    RUBRAVIEW_PNG_RGB24,
    RUBRAVIEW_PNG_PALETTE8,
    RUBRAVIEW_PNG_GRAY8,
} rubraview_png_depth_t;

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
} rubraview_export_options_t;

/** Every field at the specification's default. */
rubraview_export_options_t rubraview_export_defaults(void);

/** Clamp every field into its legal range; call after reading settings.ini. */
void rubraview_export_clamp(rubraview_export_options_t *options);

/** The file extension a format writes, without the dot ("jpg", "png", ...). */
u8str_t rubraview_export_extension(rubraview_export_format_t format);

/** The format a filename implies, or SAME_AS_SOURCE when it names none. */
rubraview_export_format_t rubraview_export_format_for_name(u8str_t filename);

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
