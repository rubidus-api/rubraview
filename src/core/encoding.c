#include "rubraview/encoding.h"
#include "rubraview/utf8.h"

rubraview_encoding_verdict_t rubraview_archive_filename_detect(u8str_t raw_name, bool utf8_flag) {
    if (utf8_flag) {
        return RUBRAVIEW_ENCODING_UTF8_FLAGGED;
    }
    if (rubraview_utf8_validate(raw_name)) {
        return RUBRAVIEW_ENCODING_UTF8_VALID;
    }
    return RUBRAVIEW_ENCODING_NEEDS_FALLBACK;
}
