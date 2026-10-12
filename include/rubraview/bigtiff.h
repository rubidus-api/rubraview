#ifndef RUBRAVIEW_BIGTIFF_H
#define RUBRAVIEW_BIGTIFF_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * RV-110: a BigTIFF read by a decoder that knows only TIFF. BigTIFF is
 * TIFF with eight-byte offsets, for files past 4 GB; one that is smaller
 * holds nothing an ordinary TIFF could not. Nothing is decoded here: the
 * file's directories are written out again in the ordinary form, after
 * the file's own bytes, and its first eight bytes are made an ordinary
 * TIFF's header that points at them. The picture data stays where it is.
 *
 * Left out of the new directories: the tags that point at further
 * directories (sub-pictures, EXIF, GPS), which are still in BigTIFF's
 * form where they lie.
 */

/* The eight bytes of a BigTIFF's header ("II" 43 or "MM" 43, offsets of 8). */
bool rubraview_bigtiff_is(const uint8_t *bytes, size_t size);

/* How many bytes the ordinary directories take after the file's own;
   0 when the file is not a BigTIFF that can be read this way (damaged,
   a value that does not fit in four bytes, or past 4 GB with them). */
size_t rubraview_bigtiff_classic_extra(const uint8_t *bytes, size_t size);

/* Writes the ordinary TIFF into `out`, which holds `size` plus the
   extra bytes: the file's bytes, the new header, the directories. */
bool rubraview_bigtiff_to_classic(const uint8_t *bytes, size_t size, uint8_t *out, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif
