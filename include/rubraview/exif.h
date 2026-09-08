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
