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
 * Step 3 (fall back to a host code page such as CP949/Shift-JIS when the
 * bytes are not valid UTF-8) is RV-046 in M4 — it needs the Windows
 * MultiByteToWideChar bridge and is out of scope here. This module only
 * classifies which of the two host-independent outcomes applies.
 */
typedef enum rubraview_encoding_verdict {
    RUBRAVIEW_ENCODING_UTF8_FLAGGED = 0, /* GP Bit 11 set: trust as UTF-8 without validation */
    RUBRAVIEW_ENCODING_UTF8_VALID,       /* not flagged, but the bytes validate as UTF-8 */
    RUBRAVIEW_ENCODING_NEEDS_FALLBACK,   /* not flagged and not valid UTF-8: needs RV-046 */
} rubraview_encoding_verdict_t;

rubraview_encoding_verdict_t rubraview_archive_filename_detect(u8str_t raw_name, bool utf8_flag);

#ifdef __cplusplus
}
#endif

#endif /* RUBRAVIEW_ENCODING_H */
