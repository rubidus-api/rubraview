#ifndef RUBRAVIEW_EXIF_H
#define RUBRAVIEW_EXIF_H

#include "rubraview/core.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * EXIF orientation reading and lossless JPEG privacy marker stripping
 * (RFC-0001 §3.9 auto-detection, §3.10 privacy clean). Both operate by
 * walking JPEG marker segments up to SOS (Start of Scan) — the
 * entropy-coded pixel data after SOS is never parsed or touched, so
 * stripping is exactly lossless: DCT coefficients are never decoded.
 */

/**
 * Parse the EXIF `0x0112` Orientation tag from an APP1/Exif segment.
 * Returns 1-8 on success, or 1 (the identity/"normal" orientation,
 * TIFF's own default when the tag is absent) if the buffer is not a
 * JPEG, has no Exif APP1 segment, or the tag is absent/unparseable.
 */
int32_t rubraview_exif_read_orientation(const uint8_t *jpeg, size_t size);

/**
 * What a picture's EXIF says, for the file's information window (owner,
 * 2026-09-29). Strings are copied in, NUL-terminated, trimmed; a number
 * the file does not give is 0 or its `has_` flag false.
 */
typedef struct rubraview_exif_info {
    bool     found;
    char     make[64], model[64], lens[96], software[64], artist[64], copyright[96];
    char     date_taken[24], date_modified[24];   /* "2024-05-01 13:22:07" */
    int32_t  orientation;                         /* 1-8, 0 when not given */
    bool     has_exposure; uint32_t exposure_num, exposure_den;   /* seconds, as a fraction */
    bool     has_fnumber;  double fnumber;
    uint32_t iso;
    bool     has_focal;    double focal_mm;
    uint32_t focal_35mm;
    bool     has_bias;     double exposure_bias;  /* EV */
    bool     has_flash;    uint16_t flash;        /* the EXIF bits: bit 0 fired */
    uint32_t pixel_x, pixel_y;
    bool     has_gps;      double latitude, longitude;   /* degrees, south and west negative */
    bool     has_altitude; double altitude_m;
    /* More of what a camera writes (owner, 2026-09-29: "exif 정보나 ... GPS정보 등 다양한 정보들이 다 보여야"). */
    char     description[96], lens_make[64], serial[48];
    char     date_digitized[24], offset_time[8];         /* "+09:00" */
    uint16_t exposure_program, metering, white_balance, exposure_mode, scene_type, color_space;
    bool     has_digital_zoom; double digital_zoom;
    char     gps_time[32];                                /* "2024-05-01 04:22:07 UTC" */
    bool     has_direction; double direction; char direction_ref;   /* degrees; 'T' true or 'M' magnetic north */
    bool     has_speed;     double speed; char speed_ref;           /* 'K' km/h, 'M' mph, 'N' knots */
} rubraview_exif_info_t;

/**
 * Read the EXIF block of a JPEG (APP1), a TIFF (the file is the block), a
 * PNG (`eXIf`), a WebP (`EXIF`), or anything else carrying "Exif\0\0"
 * near its start (HEIF). Every offset is checked against `size`; a
 * sub-IFD is followed one level and never back into itself. False, with
 * `found` false, when there is none.
 */
bool rubraview_exif_read(const uint8_t *data, size_t size, rubraview_exif_info_t *out);

typedef struct rubraview_jpeg_strip_result {
    u8str_t data;           /* arena-allocated JPEG bytes with the targeted segments removed */
    bool    stripped_exif;  /* an APP1 segment with the "Exif\0\0" signature was removed */
    bool    stripped_xmp;   /* an APP1 segment with the XMP "http://ns.adobe.com/xap/1.0/\0" signature was removed */
    bool    stripped_iptc;  /* an APP13 segment (the conventional Photoshop/IPTC marker) was removed */
} rubraview_jpeg_strip_result_t;

/**
 * Return a copy of `jpeg` with its EXIF, XMP, and IPTC marker segments
 * removed, without decoding or re-encoding any pixel data. On a buffer
 * that is not a well-formed JPEG (bad SOI, or a marker walk that cannot
 * proceed), returns the trailing bytes copied through unchanged from the
 * point parsing stopped, with no flags set — never corrupts, only fails
 * to strip.
 */
rubraview_jpeg_strip_result_t rubraview_jpeg_privacy_strip(proven_arena_t *arena, const uint8_t *jpeg, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_EXIF_H */
