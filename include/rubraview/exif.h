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
    /* RV-130 (owner, 2026-10-17, after IrfanView's EXIF page): the rest a
       camera commonly writes. A word-valued tag that may be 0 is kept +1,
       so 0 still means not given. */
    char     exif_version[8];                             /* "2.31" */
    bool     has_resolution; double x_resolution, y_resolution; uint16_t resolution_unit;   /* 2 inch, 3 cm */
    bool     has_max_aperture; double max_aperture;      /* as an f-number */
    bool     has_subject_distance; double subject_distance_m;   /* < 0: infinity */
    bool     has_brightness; double brightness;          /* APEX */
    uint16_t light_source;                                /* +1 */
    uint16_t contrast, saturation, sharpness, gain_control, distance_range, custom_rendered;   /* +1 */
    uint16_t sensing_method, file_source;                 /* as written; 0 not given */
    char     unique_id[40], owner[64], user_comment[128];
    bool     has_lens_spec; double lens_spec[4];          /* shortest, longest mm; f at each */
    char     gps_datum[24];
} rubraview_exif_info_t;

/**
 * What a picture's IPTC block says (RV-130): the record-2 fields a photo
 * agency or an editor writes. Read from a JPEG's Photoshop APP13 block
 * (resource 0x0404) or a TIFF's tag 33723. Text is UTF-8: a block that
 * does not say UTF-8 and is not valid UTF-8 is read as Latin-1.
 */
typedef struct rubraview_iptc_info {
    bool found;
    char title[96], headline[160], caption[512], keywords[384], category[16];
    char byline[96], byline_title[96], writer[96], credit[96], source[96], copyright[160];
    char city[64], sublocation[64], state[64], country[64], country_code[8];
    char date_created[24], instructions[256], job_id[64];
} rubraview_iptc_info_t;

/** False, with `found` false, when the file holds no IPTC block. */
bool rubraview_iptc_read(const uint8_t *data, size_t size, rubraview_iptc_info_t *out);

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
