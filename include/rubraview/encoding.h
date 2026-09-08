#ifndef RUBRAVIEW_ENCODING_H
#define RUBRAVIEW_ENCODING_H

#include "rubraview/core.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Archive filename encoding detection, §3.8.3 steps 1-2:
 *   1. If the ZIP General Purpose Bit 11 (UTF-8 flag) is set, trust the
 *      name as UTF-8.
 *   2. Otherwise, validate the byte stream against strict UTF-8 rules;
 *      valid bytes are kept as UTF-8.
 *   3. If the bytes are not valid UTF-8, transcode them from a legacy
 *      code page — automatically the host's own, or whichever the reader
 *      forced from the Menu Box (RV-046).
 *
 * The decision of *which* code page applies is here and testable; the
 * transcoding itself is an OS call (MultiByteToWideChar on Windows) and
 * lives behind rubraview_pal_transcode_codepage.
 */
typedef enum rubraview_encoding_verdict {
    RUBRAVIEW_ENCODING_UTF8_FLAGGED = 0, /* GP Bit 11 set: trust as UTF-8 without validation */
    RUBRAVIEW_ENCODING_UTF8_VALID,       /* not flagged, but the bytes validate as UTF-8 */
    RUBRAVIEW_ENCODING_NEEDS_FALLBACK,   /* not flagged and not valid UTF-8: needs RV-046 */
} rubraview_encoding_verdict_t;

rubraview_encoding_verdict_t rubraview_archive_filename_detect(u8str_t raw_name, bool utf8_flag);

/**
 * §3.8.3's explicit override list, as offered under
 * `Menu > Archive > Filename Encoding`.
 */
typedef enum rubraview_codepage {
    RUBRAVIEW_CODEPAGE_AUTO = 0,   /* the host's active ANSI code page */
    RUBRAVIEW_CODEPAGE_UTF8,       /* force UTF-8 even when the bytes look legacy */
    RUBRAVIEW_CODEPAGE_KOREAN,     /* CP949 / EUC-KR */
    RUBRAVIEW_CODEPAGE_JAPANESE,   /* Shift-JIS / CP932 */
    RUBRAVIEW_CODEPAGE_SIMPLIFIED_CHINESE,  /* GBK / CP936 */
    RUBRAVIEW_CODEPAGE_TRADITIONAL_CHINESE, /* Big5 / CP950 */
    RUBRAVIEW_CODEPAGE_WESTERN,    /* CP1252 / ISO-8859-1 */
} rubraview_codepage_t;

/** The numeric Windows code page an override selects; 0 means "the host's own". */
uint32_t rubraview_codepage_id(rubraview_codepage_t codepage);

/** The label shown on the override's menu tile. */
u8str_t rubraview_codepage_label(rubraview_codepage_t codepage);

/**
 * Decide how an archive entry's name should be read, combining the
 * verdict with the reader's override. Returns true when the name needs
 * transcoding and writes the code page to use; false when the bytes are
 * already UTF-8 and must be taken as they are.
 *
 * An explicit UTF-8 override wins over the byte evidence — that is the
 * point of an override — while AUTO trusts the flag and the validator
 * first, exactly as §3.8.3 steps 1 and 2 describe.
 */
bool rubraview_archive_filename_plan(u8str_t raw_name, bool utf8_flag,
                                     rubraview_codepage_t override_choice,
                                     uint32_t *out_codepage_id);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_ENCODING_H */
